/* wmnm - NetworkManager frontend as a Window Maker dockapp
 * Copyright (C) 2018 Doug Torrance <dtorrance@piedmont.edu>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 */

#include <NetworkManager.h>
#include <nm-secret-agent-old.h>
#include <string.h>

#include "wmnm-agent.h"
#include "wmnm-askpass-client.h"

#define WMNM_AGENT_IDENTIFIER "edu.piedmont.wmnm"

/* G_DECLARE_FINAL_TYPE() emits a g_autoptr cleanup function that needs one
   for the parent type too, and libnm does not define one for
   NMSecretAgentOld. */
G_DEFINE_AUTOPTR_CLEANUP_FUNC(NMSecretAgentOld, g_object_unref)

#define WMNM_TYPE_AGENT (wmnm_agent_get_type())
G_DECLARE_FINAL_TYPE(WmnmAgent, wmnm_agent, WMNM, AGENT, NMSecretAgentOld)

struct _WmnmAgent {
	NMSecretAgentOld parent;
	GHashTable *pending;		/* request key -> Request * */
};

G_DEFINE_TYPE(WmnmAgent, wmnm_agent, NM_TYPE_SECRET_AGENT_OLD)

typedef struct {
	WmnmAgent *agent;		/* not referenced; outlives requests */
	NMConnection *connection;
	char *key;
	char *setting_name;
	char *property;
	NMSecretAgentOldGetSecretsFunc callback;
	gpointer callback_data;
	WmnmAskpass *askpass;
} Request;

static WmnmAgent *the_agent;

static char *request_key(const char *path, const char *setting_name)
{
	return g_strdup_printf("%s\n%s", path, setting_name);
}

static void request_free(gpointer data)
{
	Request *request = data;

	g_clear_object(&request->connection);
	g_free(request->key);
	g_free(request->setting_name);
	g_free(request->property);
	g_free(request);
}

static void fail_request(NMSecretAgentOld *self, NMConnection *connection,
			 NMSecretAgentOldGetSecretsFunc callback,
			 gpointer callback_data, const char *message)
{
	GError *error;

	error = g_error_new_literal(NM_SECRET_AGENT_ERROR,
				    NM_SECRET_AGENT_ERROR_NO_SECRETS, message);
	callback(self, connection, NULL, error, callback_data);
	g_error_free(error);
}

/* NM_VARIANT_TYPE_CONNECTION is a{sa{sv}}: setting name to property map.
   Building it directly avoids the verification surprises of running
   nm_connection_to_dbus() over a connection that holds only a security
   setting. */
static GVariant *build_secrets(const char *setting_name, const char *property,
			       const char *secret)
{
	GVariantBuilder settings, properties;

	g_variant_builder_init(&properties, G_VARIANT_TYPE("a{sv}"));
	g_variant_builder_add(&properties, "{sv}", property,
			      g_variant_new_string(secret));

	g_variant_builder_init(&settings, G_VARIANT_TYPE("a{sa{sv}}"));
	g_variant_builder_add(&settings, "{sa{sv}}", setting_name,
			      &properties);

	return g_variant_builder_end(&settings);
}

static void on_secret(const char *secret, gpointer user_data)
{
	Request *request = user_data;
	GVariant *secrets;

	request->askpass = NULL;

	if (!secret) {
		GError *error;

		error = g_error_new_literal(NM_SECRET_AGENT_ERROR,
					    NM_SECRET_AGENT_ERROR_USER_CANCELED,
					    "cancelled");
		request->callback(NM_SECRET_AGENT_OLD(request->agent),
				  request->connection, NULL, error,
				  request->callback_data);
		g_error_free(error);
	} else {
		secrets = build_secrets(request->setting_name,
					request->property, secret);
		request->callback(NM_SECRET_AGENT_OLD(request->agent),
				  request->connection, secrets, NULL,
				  request->callback_data);
	}

	g_hash_table_remove(request->agent->pending, request->key);
}

/* Returns the secret the connection already carries, if any. */
static const char *existing_secret(NMConnection *connection,
				   const char *setting_name,
				   const char *property)
{
	NMSetting *setting;
	char *value = NULL;

	setting = nm_connection_get_setting_by_name(connection, setting_name);
	if (!setting)
		return NULL;

	g_object_get(setting, property, &value, NULL);
	if (value && *value)
		return value;

	g_free(value);

	return NULL;
}

static void wmnm_agent_get_secrets(NMSecretAgentOld *self,
				   NMConnection *connection,
				   const char *connection_path,
				   const char *setting_name,
				   const char **hints,
				   NMSecretAgentGetSecretsFlags flags,
				   NMSecretAgentOldGetSecretsFunc callback,
				   gpointer callback_data)
{
	WmnmAgent *agent = WMNM_AGENT(self);
	Request *request;
	const char *property, *existing, *id;
	char *prompt, *key;

	/* Anything that is not a wifi passphrase belongs to some other agent:
	   say we have nothing so NetworkManager keeps looking. */
	if (g_strcmp0(setting_name,
		      NM_SETTING_WIRELESS_SECURITY_SETTING_NAME) != 0) {
		fail_request(self, connection, callback, callback_data,
			     "wmnm only handles 802-11-wireless-security");
		return;
	}

	/* Without this flag NetworkManager is retrying in the background, for
	   instance autoconnecting at login.  Putting a window on screen would
	   be wrong, so decline rather than prompt. */
	if (!(flags & NM_SECRET_AGENT_GET_SECRETS_FLAG_ALLOW_INTERACTION)) {
		fail_request(self, connection, callback, callback_data,
			     "no interaction allowed");
		return;
	}

	/* hints names exactly what NetworkManager wants: psk, wep-key0 and so
	   on.  Fall back to the common case. */
	property = (hints && hints[0]) ? hints[0] : "psk";

	/* REQUEST_NEW means what we supplied last time was rejected, so do not
	   hand back the same thing again. */
	if (!(flags & NM_SECRET_AGENT_GET_SECRETS_FLAG_REQUEST_NEW)) {
		existing = existing_secret(connection, setting_name, property);
		if (existing) {
			GVariant *secrets;

			secrets = build_secrets(setting_name, property,
						existing);
			callback(self, connection, secrets, NULL,
				 callback_data);
			g_free((char *)existing);
			return;
		}
	}

	key = request_key(connection_path, setting_name);
	if (g_hash_table_contains(agent->pending, key)) {
		g_free(key);
		fail_request(self, connection, callback, callback_data,
			     "already asking");
		return;
	}

	id = nm_connection_get_id(connection);
	if (flags & NM_SECRET_AGENT_GET_SECRETS_FLAG_REQUEST_NEW)
		prompt = g_strdup_printf(
			"Wrong password.  Password for \"%s\":", id);
	else
		prompt = g_strdup_printf("Password for \"%s\":", id);

	request = g_new0(Request, 1);
	request->agent = agent;
	request->connection = g_object_ref(connection);
	request->key = key;
	request->setting_name = g_strdup(setting_name);
	request->property = g_strdup(property);
	request->callback = callback;
	request->callback_data = callback_data;

	request->askpass = wmnm_askpass_run(prompt, on_secret, request);
	g_free(prompt);

	if (!request->askpass) {
		request_free(request);
		fail_request(self, connection, callback, callback_data,
			     "no askpass helper available");
		return;
	}

	g_hash_table_insert(agent->pending, request->key, request);
}

static void wmnm_agent_cancel_get_secrets(NMSecretAgentOld *self,
					  const char *connection_path,
					  const char *setting_name)
{
	WmnmAgent *agent = WMNM_AGENT(self);
	Request *request;
	char *key;

	key = request_key(connection_path, setting_name);
	request = g_hash_table_lookup(agent->pending, key);
	g_free(key);

	if (!request)
		return;

	/* Since libnm 1.24 the get_secrets callback is ignored once a request
	   has been cancelled, so it is deliberately not invoked here. */
	if (request->askpass)
		wmnm_askpass_cancel(request->askpass);

	g_hash_table_remove(agent->pending, request->key);
}

/* NetworkManager persists secrets itself; we have nothing of our own to store.
   The vfuncs still have to complete, or the caller waits forever. */
static void wmnm_agent_save_secrets(NMSecretAgentOld *self,
				    NMConnection *connection,
				    const char *connection_path,
				    NMSecretAgentOldSaveSecretsFunc callback,
				    gpointer callback_data)
{
	(void)connection_path;

	callback(self, connection, NULL, callback_data);
}

static void wmnm_agent_delete_secrets(NMSecretAgentOld *self,
				      NMConnection *connection,
				      const char *connection_path,
				      NMSecretAgentOldDeleteSecretsFunc callback,
				      gpointer callback_data)
{
	(void)connection_path;

	callback(self, connection, NULL, callback_data);
}

static void wmnm_agent_init(WmnmAgent *agent)
{
	agent->pending = g_hash_table_new_full(g_str_hash, g_str_equal, NULL,
					       request_free);
}

static void wmnm_agent_finalize(GObject *object)
{
	WmnmAgent *agent = WMNM_AGENT(object);

	g_clear_pointer(&agent->pending, g_hash_table_destroy);

	G_OBJECT_CLASS(wmnm_agent_parent_class)->finalize(object);
}

static void wmnm_agent_class_init(WmnmAgentClass *klass)
{
	NMSecretAgentOldClass *agent_class = NM_SECRET_AGENT_OLD_CLASS(klass);

	G_OBJECT_CLASS(klass)->finalize = wmnm_agent_finalize;

	agent_class->get_secrets = wmnm_agent_get_secrets;
	agent_class->cancel_get_secrets = wmnm_agent_cancel_get_secrets;
	agent_class->save_secrets = wmnm_agent_save_secrets;
	agent_class->delete_secrets = wmnm_agent_delete_secrets;
}

gboolean wmnm_agent_start(void)
{
	GError *error = NULL;

	the_agent = g_initable_new(WMNM_TYPE_AGENT, NULL, &error,
				   NM_SECRET_AGENT_OLD_IDENTIFIER,
				   WMNM_AGENT_IDENTIFIER,
				   NM_SECRET_AGENT_OLD_CAPABILITIES,
				   NM_SECRET_AGENT_CAPABILITY_NONE,
				   NM_SECRET_AGENT_OLD_AUTO_REGISTER, FALSE,
				   NULL);
	if (!the_agent) {
		g_message("wmnm: could not create secret agent: %s",
			  error->message);
		g_clear_error(&error);
		return FALSE;
	}

	nm_secret_agent_old_enable(NM_SECRET_AGENT_OLD(the_agent), TRUE);

	return TRUE;
}

void wmnm_agent_stop(void)
{
	if (!the_agent)
		return;

	nm_secret_agent_old_destroy(NM_SECRET_AGENT_OLD(the_agent));
	g_clear_object(&the_agent);
}
