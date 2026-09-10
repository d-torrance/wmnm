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

#include <glib-unix.h>
#include <signal.h>
#include <string.h>

#include "wmnm-askpass-client.h"

struct WmnmAskpass {
	WmnmAskpassFunc callback;
	gpointer user_data;
	GString *output;
	GPid pid;
	guint child_watch, io_watch;
	gboolean cancelled;
};

/* Note that pinentry is deliberately not consulted here.  It speaks the Assuan
   protocol on stdin and stdout rather than the askpass convention, so running
   it this way would just hang waiting for commands. */
static char *find_askpass(void)
{
	static const char *candidates[] = {
		"/usr/lib/ssh/x11-ssh-askpass",
		"/usr/lib/openssh/gnome-ssh-askpass",
		"/usr/libexec/openssh/gnome-ssh-askpass",
		"/usr/bin/ssh-askpass",
		NULL
	};
	const char *env;
	int i;

	env = g_getenv("WMNM_ASKPASS");
	if (env && *env)
		return g_strdup(env);

	/* Only trust SSH_ASKPASS when there is a display to put it on: some
	   distributions point it at a terminal helper, which would block
	   invisibly. */
	env = g_getenv("SSH_ASKPASS");
	if (g_getenv("DISPLAY") && env && *env &&
	    g_file_test(env, G_FILE_TEST_IS_EXECUTABLE))
		return g_strdup(env);

	for (i = 0; candidates[i]; i++)
		if (g_file_test(candidates[i], G_FILE_TEST_IS_EXECUTABLE))
			return g_strdup(candidates[i]);

#ifdef WMNM_ASKPASS_PATH
	if (g_file_test(WMNM_ASKPASS_PATH, G_FILE_TEST_IS_EXECUTABLE))
		return g_strdup(WMNM_ASKPASS_PATH);
#endif

	return g_find_program_in_path("wmnm-askpass");
}

static void askpass_free(WmnmAskpass *askpass)
{
	if (askpass->io_watch)
		g_source_remove(askpass->io_watch);
	if (askpass->child_watch)
		g_source_remove(askpass->child_watch);
	if (askpass->pid)
		g_spawn_close_pid(askpass->pid);

	/* Best effort: do not leave the secret lying around in freed heap. */
	memset(askpass->output->str, 0, askpass->output->allocated_len);
	g_string_free(askpass->output, TRUE);
	g_free(askpass);
}

static gboolean on_askpass_output(gint fd, GIOCondition condition, gpointer user_data)
{
	WmnmAskpass *askpass = user_data;
	char buffer[256];
	gssize n;

	if (condition & G_IO_IN) {
		n = read(fd, buffer, sizeof(buffer));
		if (n > 0) {
			g_string_append_len(askpass->output, buffer, n);
			memset(buffer, 0, sizeof(buffer));
			return G_SOURCE_CONTINUE;
		}
	}

	askpass->io_watch = 0;

	return G_SOURCE_REMOVE;
}

static void on_askpass_exit(GPid pid, gint status, gpointer user_data)
{
	WmnmAskpass *askpass = user_data;
	char *secret = NULL;

	(void)pid;

	askpass->child_watch = 0;

	if (askpass->cancelled) {
		askpass_free(askpass);
		return;
	}

	if (g_spawn_check_wait_status(status, NULL) &&
	    askpass->output->len > 0) {
		g_strchomp(askpass->output->str);
		secret = askpass->output->str;
	}

	askpass->callback(secret, askpass->user_data);
	askpass_free(askpass);
}

WmnmAskpass *wmnm_askpass_run(const char *prompt, WmnmAskpassFunc callback,
			      gpointer user_data)
{
	WmnmAskpass *askpass;
	char *helper;
	char *argv[3];
	GError *error = NULL;
	GPid pid;
	gint out_fd;

	helper = find_askpass();
	if (!helper) {
		g_message("wmnm: no askpass helper found");
		return NULL;
	}

	argv[0] = helper;
	argv[1] = (char *)prompt;
	argv[2] = NULL;

	/* The secret comes back on stdout.  It must never be passed on the
	   command line or in the environment, both of which are readable by
	   other processes through /proc. */
	if (!g_spawn_async_with_pipes(NULL, argv, NULL,
				      G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL,
				      &pid, NULL, &out_fd, NULL, &error)) {
		g_message("wmnm: could not run %s: %s", helper, error->message);
		g_clear_error(&error);
		g_free(helper);
		return NULL;
	}
	g_free(helper);

	askpass = g_new0(WmnmAskpass, 1);
	askpass->callback = callback;
	askpass->user_data = user_data;
	askpass->output = g_string_new(NULL);
	askpass->pid = pid;
	askpass->io_watch = g_unix_fd_add(out_fd, G_IO_IN | G_IO_HUP,
					  on_askpass_output, askpass);
	askpass->child_watch = g_child_watch_add(pid, on_askpass_exit, askpass);

	return askpass;
}

void wmnm_askpass_cancel(WmnmAskpass *askpass)
{
	if (!askpass || askpass->cancelled)
		return;

	askpass->cancelled = TRUE;

	if (askpass->io_watch) {
		g_source_remove(askpass->io_watch);
		askpass->io_watch = 0;
	}

	/* Leave the child watch in place so the process is reaped and the
	   request freed there. */
	if (askpass->pid)
		kill(askpass->pid, SIGTERM);
}
