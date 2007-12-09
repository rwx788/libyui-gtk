/********************************************************************
 *           YaST2-GTK - http://en.opensuse.org/YaST2-GTK           *
 ********************************************************************/

#ifndef YGUTILS_H
#define YGUTILS_H

#include <string>
#include <list>
#include <gtk/gtktextview.h>
#include <gtk/gtkentry.h>
#include <gtk/gtktreeview.h>
#include <gtk/gtktreemodel.h>
#include <gtk/gtkcellrenderertoggle.h>

// TODO: do a cleanup here. We should probably split string, gtk and stuff
// Some GtkTreeView should probably go to their own files
// Let's avoid GTK+ stuff, better to replicate that, if needed, than leave in
// this general purpose utils.

/* YGUtils.h/cc have some functionality that is shared between different parts
   of the code. */

namespace YGUtils
{
	/* Replaces Yast's '&' accelerator by Gnome's '_' (and proper escaping). */
	std::string mapKBAccel (const std::string &src);

	/* Adds filter support to a GtkEntry. */
	void setFilter (GtkEntry *entry, const std::string &validChars);

	/* Replaces every 'mouth' by 'food' in 'str'. */
	void replace (std::string &str, const char *mouth, int mouth_len, const char *food);

	/* Escapes markup text (eg. changes '<' by '\<'). */
	void escapeMarkup (std::string &str);

	/* Adds functionality to GtkTextView to scroll to bottom. */
	void scrollTextViewDown(GtkTextView *text_view);

	/* Returns the average width of the given number of characters in pixels. */
	int getCharsWidth (GtkWidget *widget, int chars_nb);
	int getCharsHeight (GtkWidget *widget, int chars_nb);

	/* Sets some widget font proprities. */
	void setWidgetFont (GtkWidget *widget, PangoWeight weight, double scale);

	/* A more sane strcmp() from the user point of view that honors numbers.
	   i.e. "20" < "100" */
	int strcmp (const char *str1, const char *str2);

	/* Checks if a std::string contains some other string (case insensitive). */
	bool contains (const std::string &haystack, const std::string &needle);

	/* Splits a string into parts as separated by the separator characters.
	   eg: splitString ("Office/Writer", '/') => { "Office", "Writer" } */
	std::list <std::string> splitString (const std::string &str, char separator);

	/* Converts stuff to GValues */
	GValue floatToGValue (float num);

	GdkPixbuf *loadPixbuf (const std::string &fileneme);

	/* Tries to make sense out of the string, applying some stock icon to the button. */
	void setStockIcon (GtkWidget *button, std::string ycp_str);
};

extern "C" {
	int ygutils_getCharsWidth (GtkWidget *widget, int chars_nb);
	int ygutils_getCharsHeight (GtkWidget *widget, int chars_nb);
	void ygutils_setWidgetFont (GtkWidget *widget, PangoWeight weight, double scale);

	void ygutils_setFilter (GtkEntry *entry, const char *validChars);

	/* Convert html to xhtml (or at least try) */
	gchar *ygutils_convert_to_xhmlt_and_subst (const char *instr, const char *product);
	void ygutils_setStockIcon (GtkWidget *button, const char *ycp_str);
};

#endif // YGUTILS_H
