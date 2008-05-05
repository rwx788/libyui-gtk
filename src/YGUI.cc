/********************************************************************
 *           YaST2-GTK - http://en.opensuse.org/YaST2-GTK           *
 ********************************************************************/

#include <config.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <YEvent.h>
#include <YMacro.h>
#include <YCommandLine.h>
#include "YGUI.h"
#include "YGUtils.h"
#include "YGDialog.h"
#include <glib/gthread.h>

static std::string askForFileOrDirectory (GtkFileChooserAction action,
	const std::string &path, const std::string &filter, const std::string &title);

static void errorMsg (const char *msg)
{
	GtkWidget* dialog = gtk_message_dialog_new (NULL,
		GtkDialogFlags (0), GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", msg);
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
}

#define DEFAULT_MACRO_FILE_NAME  "macro.ycp"
#define BUSY_CURSOR_TIMEOUT 250
//#define PRINT_EVENTS

YGUI::YGUI (bool with_threads)
	: YUI (with_threads), m_done_init (false), busy_timeout (0)
{
	IMPL
	m_have_wm = true;
	m_no_border = m_fullscreen = false;
	m_default_size.width = m_default_size.height = 0;

	// without this none of the (default) threading action works ...
	topmostConstructorHasFinished();
}

void YGUI::checkInit()
{
	if (m_done_init)
		return;
#ifdef PRINT_EVENTS
fprintf (stderr, "init()\n");
#endif
	m_done_init = true;

	// retrieve command line args from /proc/<pid>/cmdline
	YCommandLine cmdLine;
	int argc = cmdLine.argc();
	char **argv = cmdLine.argv();

	for (int i = 1; i < argc; i++) {
		const char *argp = argv[i];
		if (!argp) continue;
		if (argp[0] != '-') {
			//printf ("Warning: Unknown argument '%s'\n", argp);
			continue;
		}
		argp++;
		if (argp[0] == '-') argp++;

		if (!strcmp (argp, "no-wm"))
			m_have_wm = false;
		else if (!strcmp (argp, "fullscreen"))
			m_fullscreen = true;
		else if (!strcmp (argp, "noborder"))
			m_no_border = true;
		else if (!strcmp (argp, "geometry")) {
			argp = argv [++i];
			if (i == argc)
				printf ("Warning: no value passed to --geometry\n");
			else if (sscanf (argp, "%dx%d", &m_default_size.width,
			                 &m_default_size.height) == EOF) {
				printf ("Warning: invalid --geometry value: %s\n", argp);
			}
		}
		else if (!strcmp (argp, "help")) {
			printf (
				 "Command line options for the YaST2 Gtk UI:\n"
				 "\n"
				 "--no-wm       assume no window manager is running\n"
				 "--noborder    no window manager border for main dialogs\n"
				 "--fullscreen  use full screen for main dialogs\n"
				 "--geomtry WxH sets a default size of W per H to main dialogs\n"
				 "--nothreads   run without additional UI threads\n"
				 "--help        prints this help text\n"
				 "\n"
				 );
			exit (0);
		}
/*
		else
			printf ("Warning: Unknown argument '--%s'\n", argp);
*/
	}

	gtk_init (&argc, &argv);
}

static inline GdkScreen *getScreen ()
{ return gdk_display_get_default_screen (gdk_display_get_default()); }
static int getDisplayWidth()
{ return gdk_screen_get_width (getScreen()); }
static int getDisplayHeight()
{ return gdk_screen_get_height (getScreen()); }

int YGUI::_getDefaultWidth()
{ 
	if (!m_default_size.width)
		m_default_size.width = MIN (600, getDisplayWidth());
	return m_default_size.width;
}

int YGUI::_getDefaultHeight()
{ 
	if (!m_default_size.height)
		m_default_size.height = MIN (450, getDisplayHeight());
	return m_default_size.height;
}

static gboolean ycp_wakeup_fn (GIOChannel *source, GIOCondition condition,
                               gpointer data)
{
	*(int *)data = TRUE;
	return TRUE;
}

void YGUI::idleLoop (int fd_ycp)
{
	IMPL
#ifdef PRINT_EVENTS
fprintf (stderr, "idleLoop()\n");
#endif
	// The rational for this is that we need somewhere to run
	// the magic 'main' thread, that can process thread unsafe
	// incoming CORBA messages for us
	checkInit();

	GIOChannel *wakeup;
	wakeup = g_io_channel_unix_new (fd_ycp);
	g_io_channel_set_encoding (wakeup, NULL, NULL);
	g_io_channel_set_buffered (wakeup, FALSE);

	int woken = FALSE;
	guint watch_tag = g_io_add_watch (wakeup, (GIOCondition)(G_IO_IN | G_IO_PRI),
	                                  ycp_wakeup_fn, &woken);
	while (!woken)
		g_main_context_iteration (NULL, TRUE);

	g_source_remove (watch_tag);
	g_io_channel_unref (wakeup);
#ifdef PRINT_EVENTS
fprintf (stderr, "no more idle\n");
#endif
}

static gboolean user_input_timeout_cb (YGUI *pThis)
{
	IMPL
	if (!pThis->pendingEvent())
		pThis->sendEvent (new YTimeoutEvent());
	return FALSE;
}

// utility that implements both userInput() and pollInput()
YEvent *YGUI::waitInput (unsigned long timeout_ms, bool block)
{
	IMPL
#ifdef PRINT_EVENTS
fprintf (stderr, "%s()\n", block ? "userInput" : "pollInput");
#endif
	checkInit();
	if (!YDialog::currentDialog (false))
		return NULL;

	if (block)
		normalCursor();  // waiting for input, so no more busy

	guint timeout = 0;
	YEvent *event = NULL;

	if (timeout_ms > 0)
		timeout = g_timeout_add (timeout_ms,
			(GSourceFunc) user_input_timeout_cb, this);

	if (block) {
		while (!pendingEvent())
			g_main_context_iteration (NULL, TRUE);
	}
	else
		while (g_main_context_iteration (NULL, FALSE)) ;

	if (pendingEvent())
		event = m_event_handler.consumePendingEvent();

	if (timeout)
		g_source_remove (timeout);

	if (block)  // if YCP keeps working for more than X time, set busy cursor
		busy_timeout = g_timeout_add (BUSY_CURSOR_TIMEOUT, busy_timeout_cb, this);

#ifdef PRINT_EVENTS
fprintf (stderr, "returning event: %s\n", !event ? "(none)" : YEvent::toString (event->eventType()));
#endif
	return event;
}

void YGUI::sendEvent (YEvent *event)
{
	m_event_handler.sendEvent (event);
	g_main_context_wakeup (NULL);
}

gboolean YGUI::busy_timeout_cb (gpointer data)
{
	YGUI *pThis = (YGUI *) data;
	pThis->busyCursor();
	pThis->busy_timeout = 0;
	return FALSE;
}

void YGUI::busyCursor()
{
	YGDialog *dialog = YGDialog::currentDialog();
	if (dialog)
		dialog->busyCursor();
}

void YGUI::normalCursor()
{
	if (busy_timeout) {
		g_source_remove (busy_timeout);
		busy_timeout = 0;
	}

	YGDialog *dialog = YGDialog::currentDialog();
	if (dialog)
		dialog->normalCursor();
}

void YGUI::makeScreenShot()
{ ((YGApplication *) app())->makeScreenShot (""); }

YEvent *YGUI::runPkgSelection (YWidget *packageSelector)
{
	yuiMilestone() << "Running package selection...\n";
	YEvent *event = 0;

	try {
		event = packageSelector->findDialog()->waitForEvent();
	} catch (const std::exception &e) {
		yuiError() << "UI::RunPkgSelection() error: " << e.what() << endl;
		yuiError() << "This is a libzypp problem. Do not file a bug against the UI!\n";
	} catch (...) {
		yuiError() << "UI::RunPkgSelection() error (unspecified)\n";
		yuiError() << "This is a libzypp problem. Do not file a bug against the UI!\n";
	}
	return event;
}

void YGUI::askPlayMacro()
{
	string filename = askForFileOrDirectory (GTK_FILE_CHOOSER_ACTION_OPEN,
		DEFAULT_MACRO_FILE_NAME, "*.ycp", "Select Macro File to Play");
	if (!filename.empty()) {
		busyCursor();
		YMacro::play (filename);
		sendEvent (new YEvent());  // flush
	}
}

void YGUI::toggleRecordMacro()
{
	if (YMacro::recording()) {
		YMacro::endRecording();
		normalCursor();

		GtkWidget* dialog = gtk_message_dialog_new (NULL,
			GtkDialogFlags (0), GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
			"Macro recording done.");
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);
	}
	else {
		string filename = askForFileOrDirectory (GTK_FILE_CHOOSER_ACTION_SAVE,
			DEFAULT_MACRO_FILE_NAME, "*.ycp", "Select Macro File to Record to");
		if (!filename.empty())
			YMacro::record (filename);
	}
}

void YGUI::askSaveLogs()
{
	string filename = askForFileOrDirectory (GTK_FILE_CHOOSER_ACTION_SAVE,
		"/tmp/y2logs.tgz", "*.tgz *.tar.gz", "Save y2logs to...");
	if (!filename.empty()) {
		std::string command = "/sbin/save_y2logs";
		command += " '" + filename + "'";
	    yuiMilestone() << "Saving y2logs: " << command << endl;
	    int ret = system (command.c_str());
		if (ret == 0)
			yuiMilestone() << "y2logs saved to " << filename << endl;
		else {
			char *error = g_strdup_printf (
				"Error: couldn't save y2logs: \"%s\" (exit value: %d)",
				command.c_str(), ret);
			yuiError() << error << endl;
			errorMsg (error);
			g_free (error);
		}
	}
}

//** YGApplication

YGApplication::YGApplication()
{
	setIconBasePath (ICON_DIR);
}

void YGApplication::makeScreenShot (string filename)
{
	IMPL
	bool interactive = filename.empty();

	GtkWidget *widget = GTK_WIDGET (YGDialog::currentWindow());
	if (!widget) {
		if (interactive)
			errorMsg ("No dialog to take screenshot of.");
		return;
	}

	GError *error = 0;
	GdkPixbuf *shot =
		gdk_pixbuf_get_from_drawable (NULL, GDK_DRAWABLE (widget->window),
			gdk_colormap_get_system(), 0, 0, 0, 0, widget->allocation.width,
			widget->allocation.height);

	if (!shot) {
		if (interactive)
			errorMsg ("Couldn't take a screenshot.");
		return;
	}

	if (interactive) {
		//** ask user for filename
		// calculate a default directory...
		if (screenShotNameTemplate.empty()) {
			string dir;
			const char *homedir = getenv("HOME");
			const char *ssdir = getenv("Y2SCREENSHOTS");
			if (!homedir || !strcmp (homedir, "/")) {
				// no homedir defined (installer)
				dir = "/tmp/" + (ssdir ? (string(ssdir)) : (string("")));
				if (mkdir (dir.c_str(), 0700) == -1)
					dir = "";
			}
			else {
				dir = homedir + (ssdir ? ("/" + string(ssdir)) : (string("")));
				mkdir (dir.c_str(), 0750);  // create a dir for what to put the pics
			}

			screenShotNameTemplate = dir + "/%s-%03d.png";
		}

		// calculate a default filename...
		const char *baseName = "yast2-";

		int nb;
		map <string, int>::iterator it = screenShotNb.find (baseName);
		if (it == screenShotNb.end())
			nb = 0;

		{
			char *tmp_name = g_strdup_printf (screenShotNameTemplate.c_str(), baseName, nb);
			filename = tmp_name;
			g_free (tmp_name);
		}
		yuiDebug() << "screenshot: " << filename << endl;

		filename = askForFileOrDirectory (
			GTK_FILE_CHOOSER_ACTION_SAVE, "", "*.png", "Save screenshot to");
		if (filename.empty()) {  // user dismissed the dialog
			yuiDebug() << "Save screen shot canceled by user\n";
			goto makeScreenShot_ret;
		}

		screenShotNb.erase (baseName);
		screenShotNb[baseName] = nb + 1;
	}

	yuiDebug() << "Saving screen shot to " << filename << endl;
	if (gdk_pixbuf_save (shot, filename.c_str(), "png", &error, NULL)) {
		yuiError() << "Couldn't save screen shot " << filename << endl;
		if (interactive) {
			string msg = "Couldn't save screenshot to file " + filename
			             + " - " + error->message;
			errorMsg (msg.c_str());
		}
		goto makeScreenShot_ret;
	}

	makeScreenShot_ret:
		g_object_unref (G_OBJECT (shot));
}

void YGApplication::beep()
{
	gdk_beep();
	GtkWindow *window = YGDialog::currentWindow();
	if (window)
		gtk_window_present (window);
}

// File/directory dialogs
#include <sstream>

std::string askForFileOrDirectory (GtkFileChooserAction action,
	const std::string &path, const std::string &filter, const std::string &title)
{
	IMPL
	GtkWidget *dialog;
	dialog = gtk_file_chooser_dialog_new (title.c_str(),
		YGDialog::currentWindow(), action, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		action == GTK_FILE_CHOOSER_ACTION_SAVE ? GTK_STOCK_SAVE : GTK_STOCK_OPEN,
		GTK_RESPONSE_ACCEPT, NULL);
	gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (dialog), TRUE);

	// filepath can be a dir or a file path, split that up
	string dirname, filename;
	if (!path.empty()) {
		if (path[0] != '/')
			yuiWarning() << "FileDialog: Relative paths are unsupported: '" << path << "'\n";
		else if (!g_file_test (path.c_str(), G_FILE_TEST_EXISTS))
			yuiWarning() << "FileDialog: Path doesn't exist: '" << path << "'\n";
		else if (g_file_test (path.c_str(), G_FILE_TEST_IS_DIR))
			dirname = path;
		else {  // its a file
			string::size_type i = path.find_last_of ("/");
			if (i != string::npos) {
				dirname = path.substr (0, i+1);
				filename = path.substr (i+1);
			}
		}
	}

	if (!dirname.empty())
		gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (dialog), dirname.c_str());
	if (!filename.empty())
		gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (dialog), filename.c_str());

	if (!filter.empty() && filter != "*") {
		GtkFileFilter *gtk_filter = gtk_file_filter_new();
		gtk_file_filter_set_name (gtk_filter, filter.c_str());
		// cut filter_pattern into chuncks like GTK likes
		std::istringstream stream (filter);
		while (!stream.eof()) {
			string str;
			stream >> str;
			if (!str.empty() && str [str.size()-1] == ',')
				str.erase (str.size()-1);
			gtk_file_filter_add_pattern (gtk_filter, str.c_str());
		}
		gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), gtk_filter);
	}

	std::string ret;
	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
		char* filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
		ret = filename;
		g_free (filename);
	}
	gtk_widget_destroy (dialog);
	return ret;
}

std::string YGApplication::askForExistingDirectory (
	const std::string &path, const std::string &title)
{
	IMPL
	return askForFileOrDirectory (GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, path,
	                              "", title);
}

std::string YGApplication::askForExistingFile (
	const std::string &path, const std::string &filter, const std::string &title)
{
	IMPL
	return askForFileOrDirectory (GTK_FILE_CHOOSER_ACTION_OPEN, path, filter, title);
}

std::string YGApplication::askForSaveFileName (
	const std::string &path, const std::string &filter, const std::string &title)
{
	IMPL
	return askForFileOrDirectory (GTK_FILE_CHOOSER_ACTION_SAVE, path, filter, title);
}

std::string YGApplication::glyph (const std::string &sym)
{
	if (sym == YUIGlyph_ArrowLeft)
		return "\u2190";
	if (sym == YUIGlyph_ArrowRight)
		return "\u2192";
	if (sym == YUIGlyph_ArrowUp)
		return "\u2191";
	if (sym == YUIGlyph_ArrowDown)
		return "\u2193";
	if (sym == YUIGlyph_CheckMark)
		return "\u2714";
	if (sym == YUIGlyph_BulletArrowRight)
		return "\u279c";
	if (sym == YUIGlyph_BulletCircle)
		return "\u274d";
	if (sym == YUIGlyph_BulletSquare)
		return "\u274f";
	return "";
}

// YWidget layout units -> pixels conversion. Same as yast-qt's.
int YGApplication::deviceUnits (YUIDimension dim, float size)
{
	if (dim == YD_HORIZ)
		size *= 640.0 / 80;
	else
		size *= 480.0 / 25;
	return size + 0.5;
}

float YGApplication::layoutUnits (YUIDimension dim, int units)
{
	float size = (float) units;
	if (dim == YD_HORIZ) return size * (80/640.0);
	else                 return size * (25/480.0);
}

int YGApplication::displayWidth()
{ return getDisplayWidth(); }

int YGApplication::displayHeight()
{ return getDisplayHeight(); }

int YGApplication::displayDepth()
{ return gdk_visual_get_best_depth(); }

long YGApplication::displayColors()
{ return 1L << displayDepth(); /*from yast-qt*/ }

// YCP writers use getDefaultWidth/Height() to do space saving if needed,
// so just tell me the displayWidth/Height(). If that size is decent, let's
// us deal with it.
int YGApplication::defaultWidth()   { return getDisplayWidth(); }
int YGApplication::defaultHeight()  { return getDisplayHeight(); }

YWidgetFactory *YGUI::createWidgetFactory()
{ return new YGWidgetFactory; }
YOptionalWidgetFactory *YGUI::createOptionalWidgetFactory()
{ return new YGOptionalWidgetFactory; }
YApplication *YGUI::createApplication()
{ return new YGApplication(); }

//** debug dialogs

//#define IS_VALID_COL
void dumpYastTree (YWidget *widget)
{
	IMPL
	struct inner {
		static void dumpYastTree (YWidget *widget, GtkTreeStore *store,
				                  GtkTreeIter *parent_node)
		{
			YGWidget *ygwidget;
			if (!widget || !(ygwidget = YGWidget::get (widget)))
				return;

			GtkTreeIter iter;
			gtk_tree_store_append (store, &iter, parent_node);

			gchar *stretch = g_strdup_printf ("%d x %d",
				widget->stretchable (YD_HORIZ), widget->stretchable (YD_VERT));
			gchar *weight = g_strdup_printf ("%d x %d",
				widget->weight (YD_HORIZ), widget->weight (YD_VERT));
			gtk_tree_store_set (store, &iter, 0, widget->widgetClass(),
				1, ygwidget->getDebugLabel().c_str(), 2, stretch, 3, weight,
#ifdef IS_VALID_COL
				4, widget->isValid(),
#endif
				-1);
			g_free (stretch);
			g_free (weight);

			for (YWidgetListConstIterator it = widget->childrenBegin();
			     it != widget->childrenEnd(); it++)
				dumpYastTree (*it, store, &iter);
		}
		static void dialog_response_cb (GtkDialog *dialog, gint response, YWidget *ywidget)
		{
			if (response == 1) {
				GtkTreeStore *store;
				GtkTreeView *view;
				store = (GtkTreeStore *) g_object_get_data (G_OBJECT (dialog), "store");
				view = (GtkTreeView *) g_object_get_data (G_OBJECT (dialog), "view");
				gtk_tree_store_clear (store);
				dumpYastTree (ywidget, store, NULL);
				gtk_tree_view_expand_all (view);
			}
			else
				gtk_widget_destroy (GTK_WIDGET (dialog));
		}
	};

	int cols = 4;
#ifdef IS_VALID_COL
	cols++;
#endif
	GtkTreeStore *store = gtk_tree_store_new (cols,
		G_TYPE_STRING, G_TYPE_STRING,
		G_TYPE_STRING, G_TYPE_STRING
#ifdef IS_VALID_COL
		, G_TYPE_BOOLEAN
#endif
		);

	GtkWidget *dialog = gtk_dialog_new_with_buttons ("YWidgets Tree", NULL,
		GtkDialogFlags (GTK_DIALOG_NO_SEPARATOR), GTK_STOCK_REFRESH, 1,
			GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE, NULL);
	gtk_window_set_default_size (GTK_WINDOW (dialog), -1, 400);

	GtkWidget *view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (store));
	gtk_tree_view_append_column (GTK_TREE_VIEW (view),
		gtk_tree_view_column_new_with_attributes ("Type",
		gtk_cell_renderer_text_new(), "text", 0, NULL));
	gtk_tree_view_append_column (GTK_TREE_VIEW (view),
		gtk_tree_view_column_new_with_attributes ("Label",
		gtk_cell_renderer_text_new(), "text", 1, NULL));
	gtk_tree_view_column_set_expand (gtk_tree_view_get_column (
		GTK_TREE_VIEW (view), 1), TRUE);
	gtk_tree_view_append_column (GTK_TREE_VIEW (view),
		gtk_tree_view_column_new_with_attributes ("Stretch",
		gtk_cell_renderer_text_new(), "text", 2, NULL));
	gtk_tree_view_append_column (GTK_TREE_VIEW (view),
		gtk_tree_view_column_new_with_attributes ("Weight",
		gtk_cell_renderer_text_new(), "text", 3, NULL));
#ifdef IS_VALID_COL
	gtk_tree_view_append_column (GTK_TREE_VIEW (view),
		gtk_tree_view_column_new_with_attributes ("Valid",
		gtk_cell_renderer_toggle_new(), "active", 4, NULL));
#endif
	gtk_tree_view_set_enable_tree_lines (GTK_TREE_VIEW (view), TRUE);

	GtkWidget *scroll_win = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scroll_win),
	                                     GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy  (GTK_SCROLLED_WINDOW (scroll_win),
	                                 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

	gtk_container_add (GTK_CONTAINER (scroll_win), view);
	gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), scroll_win);

	inner::dumpYastTree (widget, store, NULL);
	gtk_tree_view_expand_all (GTK_TREE_VIEW (view));

	g_object_set_data (G_OBJECT (dialog), "view", view);
	g_object_set_data (G_OBJECT (dialog), "store", store);
	g_signal_connect (G_OBJECT (dialog), "response",
	                  G_CALLBACK (inner::dialog_response_cb), widget);

	gtk_widget_show_all (dialog);
}

#include <YRichText.h>

void dumpYastHtml (YWidget *widget)
{
	struct inner {
		static void dumpYastHtml (YWidget *widget, GtkBox *box)
		{
			YGWidget *ygwidget;
			if (!widget || !(ygwidget = YGWidget::get (widget)))
				return;

			YRichText *rtext = dynamic_cast <YRichText *> (widget);
			if (rtext) {
				std::string text = rtext->text();
				char *xml = ygutils_convert_to_xhmlt_and_subst (text.c_str());

				GtkWidget *view = gtk_text_view_new();
				gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (view), GTK_WRAP_WORD);
				gtk_text_view_set_editable (GTK_TEXT_VIEW (view), FALSE);
				gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (view), FALSE);
				GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));
				gtk_text_buffer_set_text (buffer, xml, -1);

				GtkWidget *scroll = gtk_scrolled_window_new (NULL, NULL);
				gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scroll),
							                         GTK_SHADOW_IN);
				gtk_scrolled_window_set_policy  (GTK_SCROLLED_WINDOW (scroll),
					GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
				gtk_container_add (GTK_CONTAINER (scroll), view);
				gtk_box_pack_start (box, scroll, TRUE, TRUE, 6);

				g_free (xml);
			}

			for (YWidgetListConstIterator it = widget->childrenBegin();
			     it != widget->childrenEnd(); it++)
				dumpYastHtml (*it, box);
		}
		static void destroy_dialog (GtkDialog *dialog, gint arg)
		{ gtk_widget_destroy (GTK_WIDGET (dialog)); }
	};

	IMPL
	GtkWidget *dialog = gtk_dialog_new_with_buttons ("YWidgets HTML", NULL,
		GtkDialogFlags (GTK_DIALOG_NO_SEPARATOR), GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE, NULL);
	gtk_window_set_default_size (GTK_WINDOW (dialog), 400, 300);

	inner::dumpYastHtml (widget, GTK_BOX (GTK_DIALOG (dialog)->vbox));

	gtk_widget_show_all (dialog);
	g_signal_connect (G_OBJECT (dialog), "response",
	                  G_CALLBACK (inner::destroy_dialog), 0);
}

