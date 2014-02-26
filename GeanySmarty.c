#include <geanyplugin.h>
#include <glib.h>
#include <gdk/gdkkeysyms.h>
#include <gtk-2.0/gdk/gdkkeysyms-compat.h>

GeanyPlugin *geany_plugin;
GeanyData *geany_data;
GeanyFunctions *geany_functions;

PLUGIN_VERSION_CHECK(211)

PLUGIN_SET_INFO("Geany Smarty", 
		"Smart auto close xml/html tag, "
		"smart indent, highlight match xml/html tag, "
		"smart close brackets and quoctes, "
		"smart delete brackets and quoctes", 
		"1.0", "JLamp 07 <jlamp6789@gmail.com>");

typedef struct{
	GeanyDocument *doc;
	ScintillaObject *sci;
	gchar *config_file;
	gboolean smart_auto_close_xml_tag;
	gboolean highlight_matching_tag;
	gboolean smart_indent;
	gboolean smart_close_bracket;
	gint highlight_color;
}Smarty;

typedef struct{
	gint start;
	gint end;
	gchar *tag_name;
}Tag;

static Smarty *smarty_data = NULL;

gboolean is_open_tag(gchar *tag){
	//g_print("%s is_open_tag: %d", tag, g_regex_match(g_regex_new("< *([^/][^ ]*).*?>", 0, 0, NULL), tag, 0, NULL));
	return g_regex_match(g_regex_new("< *([^/][^ ]*).*?>", 0, 0, NULL), tag, 0, NULL);
}

gboolean is_close_tag(gchar *tag){
	//g_print("%s is_close_tag: %d", tag, g_regex_match(g_regex_new("</ *([^ ]*).*?>", 0, 0, NULL), tag, 0, NULL));
	return g_regex_match(g_regex_new("</ *([^ ]*).*?>", 0, 0, NULL), tag, 0, NULL);
}

gboolean is_comment_tag(gchar *tag){
	/*tags like <!-- --> and <!doctype ...>*/
	//g_print("%s is_comment_tag: %d", tag, g_regex_match(g_regex_new("<!.*?>", 0, 0, NULL), tag, 0, NULL));
	return g_regex_match(g_regex_new("<!.*?>", 0, 0, NULL), tag, 0, NULL);
}

gboolean is_special_tag(gchar *tag){
	/*tags like <?, <?=, <?php*/
	//g_print("%s is_special_tag: %d", tag, g_regex_match(g_regex_new("<\\?.*?>", 0, 0, NULL), tag, 0, NULL));
	return g_regex_match(g_regex_new("<\\?.*?>", 0, 0, NULL), tag, 0, NULL);
}

gboolean is_neutral_tag(gchar *tag){
	//g_print("%s is_neutral_tag: %d", tag, g_regex_match(g_regex_new("<.*?/>", 0, 0, NULL), tag, 0, NULL));
	return g_regex_match(g_regex_new("<.*?/>", 0, 0, NULL), tag, 0, NULL);
}

gchar *remove_char(gchar *tag, char remove){
	char *pr = tag, *pw = tag;
	while (*pr) {
		*pw = *pr++;
		pw += (*pw != remove);
	}
	*pw = '\0';
	
	return tag;
}

gchar *get_tag_name(gchar *tag){
	int i = 0;
	int tag_len = (gint)g_strlcpy(tag, tag, 0);
	int substr_to = tag_len - 1;
	
	while(i < tag_len){
		i++;
		if(tag[i] == ' '){
			substr_to = i;
			break;
		}
	}
	
	gchar *tag_name = g_strndup(tag, substr_to);
	
	tag_name = remove_char(tag_name, '<');
	tag_name = remove_char(tag_name, '/');
	
	return tag_name;
}

Tag find_matching_tag(ScintillaObject *sci, gint cur_pos, gboolean is_highlight, gboolean find_open_tag){
	Tag tag_data;
	tag_data.start = -1;
	tag_data.end = -1;
	tag_data.tag_name = NULL;
	
	if(cur_pos >= sci_get_length(sci) && is_highlight){
		return tag_data;
	}
	
	GRegex *regex;
	GMatchInfo *match_info;
	gchar *tag;
	gchar *string;
	GArray *open_tags = g_array_new(FALSE, FALSE, sizeof(Tag));
	gint opend_tag_size = 0;

	if(find_open_tag){
		string = sci_get_contents_range(sci, 0, cur_pos);
	}else{
		string = sci_get_contents_range(sci, cur_pos, sci_get_length(sci));
	}

	regex = g_regex_new ("<.*?>+", 0, 0, NULL);
	g_regex_match (regex, string, 0, &match_info);

	while(g_match_info_matches(match_info)){
		gint start = -1, end = -1;

		tag = g_match_info_fetch (match_info, 0);

		g_match_info_fetch_pos(match_info, 0, &start, &end);

		if(is_special_tag(tag) && sci_get_char_at(sci, start + 1) != '?'){
			tag = get_tag_name(tag);
			tag = g_strconcat("<", tag, ">", NULL);
			end = sci_find_matching_brace(sci, start) + 1;
		}

		if(!is_comment_tag(tag) && !is_neutral_tag(tag) && !is_special_tag(tag) && start != -1 && end != -1){
			if(is_open_tag(tag)){
				Tag t;
				t.start = start;
				t.end = end;
				t.tag_name = get_tag_name(tag);
				g_array_append_val(open_tags, t);
				opend_tag_size++;
			}
			if(is_close_tag(tag)){
				while(TRUE){
					if(opend_tag_size <= 0){
						tag_data.start = start;
						tag_data.end = end;
						tag_data.tag_name = get_tag_name(tag);
						return tag_data;
					}
					opend_tag_size--;
					Tag *t = &g_array_index(open_tags, Tag, opend_tag_size);
					g_array_remove_index(open_tags, opend_tag_size);

					gchar *closed_tag = get_tag_name(tag);

					if(g_strcmp0(t->tag_name, closed_tag) == 0){
						g_free(closed_tag);
						break;
					}
				}

			}
		}

		g_match_info_next (match_info, NULL);
	}

	if(opend_tag_size > 0){
		tag_data = g_array_index(open_tags, Tag, opend_tag_size - 1);
		g_array_free(open_tags, TRUE);
	}

	g_match_info_free (match_info);
	g_regex_unref (regex);
	g_free(tag);
	g_free(string);
	
	return tag_data;
}

gboolean smart_close_tag(ScintillaObject *sci, gint cur_pos, gboolean open_bracket){
	Tag tag = find_matching_tag(sci, cur_pos, FALSE, TRUE);
	
	if(tag.tag_name != NULL){
		gchar *close_tag = tag.tag_name;
		close_tag = g_strconcat("/", close_tag, NULL);
		close_tag = g_strconcat(close_tag, ">", NULL);
		if(open_bracket)
			close_tag = g_strconcat("<", close_tag, NULL);
		scintilla_send_message(sci, SCI_INSERTTEXT, cur_pos, (sptr_t)close_tag);
		if(!open_bracket)
			sci_set_current_position(sci, cur_pos + (int)g_strlcpy(close_tag, close_tag, 0), FALSE);
		
		if(smarty_data->smart_indent){
			gint current_line = sci_get_current_line(sci);
			gint open_tag_line = sci_get_line_from_position(sci, tag.start);
			
			if(current_line != open_tag_line){
				gint tag_width = sci_get_tab_width(sci);
				gint current_tag_indent = sci_get_line_indentation(sci, current_line);

				gchar *text_before_tag = sci_get_contents_range(sci, sci_get_position_from_line(sci, current_line), cur_pos - 1);
				gchar *text_indent = "";

				guint i;
				foreach_range(i, current_tag_indent / tag_width)
					text_indent = g_strconcat("\t", text_indent, NULL);

				if(g_strcmp0(text_before_tag, text_indent) == 0){
					sci_set_line_indentation(sci, current_line, sci_get_line_indentation(sci, open_tag_line));
				}
				
				g_free(text_before_tag);
				g_free(text_indent);
			}
			
		}
		return TRUE;
	}
	
	return FALSE;
}

gboolean smart_close_bracket_and_quocte(ScintillaObject *sci, gint cur_pos, gint current_ch, gint right_ch){
	switch(current_ch){
		case '}': if(right_ch == '}'){sci_set_current_position(sci, cur_pos + 1, TRUE); return TRUE;} break;
		case ')': if(right_ch == ')'){sci_set_current_position(sci, cur_pos + 1, TRUE); return TRUE;} break;
		case ']': if(right_ch == ']'){sci_set_current_position(sci, cur_pos + 1, TRUE); return TRUE;} break;
		case '\'': if(right_ch == '\''){sci_set_current_position(sci, cur_pos + 1, TRUE); return TRUE;} break;
		case '"': if(right_ch == '"'){sci_set_current_position(sci, cur_pos + 1, TRUE); return TRUE;} break;
		case '<': if(right_ch == '>'){sci_set_current_position(sci, cur_pos + 1, TRUE); return TRUE;} break;
	}
	
	return FALSE;
}

gboolean smart_delete_bracket_and_quocte(ScintillaObject *sci, gint cur_pos, gint left_ch, gint right_ch){
	switch(left_ch){
		case '{': if(right_ch == '}'){scintilla_send_message(sci, SCI_DELETERANGE, cur_pos - 1, 2); return TRUE;} break;
		case '(': if(right_ch == ')'){scintilla_send_message(sci, SCI_DELETERANGE, cur_pos - 1, 2); return TRUE;} break;
		case '[': if(right_ch == ']'){scintilla_send_message(sci, SCI_DELETERANGE, cur_pos - 1, 2); return TRUE;} break;
		case '"': if(right_ch == '"'){scintilla_send_message(sci, SCI_DELETERANGE, cur_pos - 1, 2); return TRUE;} break;
		case '\'': if(right_ch == '\''){scintilla_send_message(sci, SCI_DELETERANGE, cur_pos - 1, 2); return TRUE;} break;
		case '<': if(right_ch == '>'){scintilla_send_message(sci, SCI_DELETERANGE, cur_pos - 1, 2); return TRUE;} break;
	}
	
	return FALSE;
}

void do_indent(ScintillaObject *sci, gint cur_pos, gint current_line, gint current_line_indent, gint tag_width){
	scintilla_send_message(sci, SCI_INSERTTEXT, cur_pos, (sptr_t)"\n\n");//insert close bracket }
	sci_set_line_indentation(sci, current_line + 1, current_line_indent + tag_width);//set indent for current cursor line
	sci_set_line_indentation(sci, current_line + 2, current_line_indent);//set indent for close bracket }
	sci_set_current_position(sci, sci_get_line_end_position(sci, current_line + 1), FALSE);//move cursor to current indent
}

gboolean smart_indent(ScintillaObject *sci, gint cur_pos, gint left_char, gint right_char, gint file_type_id){
	
	gint current_line = sci_get_current_line(sci);
	gint current_line_indent = sci_get_line_indentation(sci, current_line);
	gint tag_width = sci_get_tab_width(sci);
	
	if(left_char == '>' && right_char == '<'){
		gint tag_open_start = sci_find_matching_brace(sci, cur_pos - 1);
		gint tag_close_end = sci_find_matching_brace(sci, cur_pos);
		gchar *open_tag = sci_get_contents_range(sci, tag_open_start, cur_pos);
		gchar *close_tag = sci_get_contents_range(sci, cur_pos, tag_close_end + 1);
		if(is_open_tag(open_tag) && is_close_tag(close_tag) && g_strcmp0(get_tag_name(open_tag), get_tag_name(close_tag)) == 0){
			sci_start_undo_action(sci);
			do_indent(sci, cur_pos, current_line, current_line_indent, tag_width);
			sci_end_undo_action(sci);
			g_free(open_tag);
			g_free(close_tag);
			return TRUE;
		}
	}
	
	if(left_char == '{' || left_char == '('){
		g_print("%d, %d", current_line_indent, sci_get_line_indentation(sci, current_line + 1));
		
		if(current_line_indent + tag_width == sci_get_line_indentation(sci, current_line + 1)){
			return FALSE;
		}
		
		sci_start_undo_action(sci);
		
		if(left_char == '{' && right_char != '}'){
			scintilla_send_message(sci, SCI_INSERTTEXT, cur_pos, (sptr_t)"}");
		}
		
		if(left_char == '(' && right_char != ')'){
			scintilla_send_message(sci, SCI_INSERTTEXT, cur_pos, (sptr_t)")");
		}
		
		do_indent(sci, cur_pos, current_line, current_line_indent, tag_width);
		
		sci_end_undo_action(sci);
		return TRUE;
	}
	
	return FALSE;
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data){
	Smarty *data = user_data;
	g_return_if_fail(data);
	gint left_ch, current_ch, right_ch, cur_pos;
	
	cur_pos = sci_get_current_position(data->sci);
	current_ch = event->keyval;
	left_ch = sci_get_char_at(data->sci, cur_pos - 1);
	right_ch = sci_get_char_at(data->sci, cur_pos);
	
	if(smarty_data->smart_auto_close_xml_tag){
		switch(data->doc->file_type->id){
			case 23://HTML
				if(current_ch == '/'){
					if(left_ch == '<'){
						return smart_close_tag(data->sci, cur_pos, FALSE);
					}
				}else if(current_ch == '>'){
					if(left_ch == '<') return FALSE;
					scintilla_send_message(data->sci, SCI_INSERTTEXT, cur_pos, (sptr_t)">");
					sci_set_current_position(data->sci, ++cur_pos, TRUE);
					return smart_close_tag(data->sci, cur_pos, TRUE);
				}
		}
	}
	
	if(smarty_data->smart_indent && current_ch == GDK_Return){
		return smart_indent(data->sci, cur_pos, left_ch, right_ch, data->doc->file_type->id);
	}
	
	if(smarty_data->smart_close_bracket){
		if(current_ch == GDK_BackSpace){
			return smart_delete_bracket_and_quocte(data->sci, cur_pos, left_ch, right_ch);
		}
		
		return smart_close_bracket_and_quocte(data->sci, cur_pos, current_ch, right_ch);
	}

	return FALSE;
}


//====================HILIGHLIGHT MATCHING TAG=====================
void clear_highlighting(ScintillaObject *sci){
    scintilla_send_message(sci, SCI_SETINDICATORCURRENT, 9, 0);
    scintilla_send_message(sci, SCI_INDICATORCLEARRANGE, 0, sci_get_length(sci));
}

gint convert_to_bgr(gint color){
    guint r, g, b;

    r = color >> 16;
    g = (0x00ff00 & color) >> 8;
    b = (0x0000ff & color);

    color = (r | (g << 8) | (b << 16));

    return color;
}

void highlight(ScintillaObject *sci, gint start, gint end, gint color){
    scintilla_send_message(sci, SCI_SETINDICATORCURRENT, 9, 0);
    scintilla_send_message(sci, SCI_INDICSETSTYLE, 9, INDIC_ROUNDBOX);
    scintilla_send_message(sci, SCI_INDICSETFORE, 9, convert_to_bgr(color));
    scintilla_send_message(sci, SCI_INDICSETALPHA, 9, 60);
    scintilla_send_message(sci, SCI_INDICATORFILLRANGE, start, end-start);
}

void highlight_matching_tag(ScintillaObject *sci, gint cur_pos){
	gint left_ch = sci_get_char_at(sci, cur_pos - 1);
	if(left_ch == '>') cur_pos--;
	gint right_ch = sci_get_char_at(sci, cur_pos);
	gint tag_start = -1, tag_end = -1;
	gint cpos = cur_pos;
	
	
	if(right_ch == '>'){
		tag_start = sci_find_matching_brace(sci, cur_pos);
		tag_end = cur_pos + 1;
	}else if(right_ch == '<'){
		tag_start = cur_pos;
		tag_end = sci_find_matching_brace(sci, cur_pos) + 1;
	}else{
		gint current_line = sci_get_current_line(sci);
		gint line_start_pos = sci_get_position_from_line(sci, current_line);
		
		while(TRUE){
			cpos--;
			
			if(cpos < line_start_pos){
				tag_start = -1;
				tag_end = -1;
				break;
			}
			
			right_ch = sci_get_char_at(sci, cpos);
			if(right_ch == '<'){
				tag_start = cpos;
				tag_end = sci_find_matching_brace(sci, cpos) + 1;
				break;
			}
		}
	}
	
	if(tag_start != -1 && tag_end != -1 && tag_start < tag_end){
		
		gchar *tag = sci_get_contents_range(sci, tag_start, tag_end);
		
		if(is_special_tag(tag) && sci_get_char_at(sci, tag_start + 1) != '?'){
			tag = get_tag_name(tag);
			tag = g_strconcat("<", tag, ">", NULL);
			tag_end = sci_find_matching_brace(sci, tag_start) + 1;
		}
		
		if(
			tag != NULL && 
			!g_regex_match(g_regex_new("<.*?>.*?</.*?>", 0, 0, NULL), tag, 0, NULL) && 
			!is_comment_tag(tag) &&
			!is_special_tag(tag)
		){
			
			Tag tag_data;
			if(is_close_tag(tag)){
				tag_data = find_matching_tag(sci, cur_pos, TRUE, TRUE);//find open tag
			}else if(is_open_tag(tag)){
				tag_data = find_matching_tag(sci, tag_end, TRUE, FALSE);//find close tag
				tag_data.start = tag_data.start + tag_end;
				tag_data.end = tag_data.end + tag_end;
			}
			
			if(tag_data.start > -1 && tag_data.end <= sci_get_length(sci) && tag_data.start < tag_data.end){
				highlight(sci, tag_start, tag_end, smarty_data->highlight_color);
				highlight(sci, tag_data.start, tag_data.end, smarty_data->highlight_color);
			}
		}
	}
}

static void on_sci_notify(ScintillaObject *sci, gint scn, SCNotification *nt, gpointer user_data){
	Smarty *data = user_data;
	g_return_if_fail(data);
	
	gboolean updated_sel  = nt->updated & SC_UPDATE_SELECTION;
	gboolean updated_text = nt->updated & SC_UPDATE_CONTENT;
	
	if (updated_sel && !updated_text && smarty_data->highlight_matching_tag){
		switch(data->doc->file_type->id){
			case 23:{//HTML
				clear_highlighting(sci);
				highlight_matching_tag(sci, sci_get_current_position(sci));
				break;
			}
		}
	}
}

static void on_document_open(GObject *obj, GeanyDocument *doc, gpointer user_data){
	g_return_if_fail(DOC_VALID(doc));
	
	Smarty *data;
	data = g_new0(Smarty, 1);
	data->doc = doc;
	data->sci = doc->editor->sci;
	plugin_signal_connect(geany_plugin, G_OBJECT(data->sci), "key-press-event", FALSE, G_CALLBACK(on_key_press), data);
	plugin_signal_connect(geany_plugin, G_OBJECT(data->sci), "sci-notify", FALSE, G_CALLBACK(on_sci_notify), data);
}

PluginCallback plugin_callbacks[] = {
	{ "document-open",  (GCallback) &on_document_open, FALSE, NULL },
	{ "document-new",   (GCallback) &on_document_open, FALSE, NULL },
	{ NULL, NULL, FALSE, NULL }
};

void plugin_init(GeanyData *data){
	int i;
	foreach_document(i){
		on_document_open(NULL, documents[i], NULL);
	}
	
	GKeyFile *config = g_key_file_new();
	
	smarty_data = g_new0(Smarty, 1);

	smarty_data->config_file = g_strconcat(geany->app->configdir, G_DIR_SEPARATOR_S,
		"plugins", G_DIR_SEPARATOR_S, "geany-smarty", G_DIR_SEPARATOR_S, "geany-smarty.conf", NULL);

	g_key_file_load_from_file(config, smarty_data->config_file, G_KEY_FILE_NONE, NULL);
	
	smarty_data->smart_auto_close_xml_tag = utils_get_setting_boolean(config, "geany-smarty", "smart_auto_close_xml_tag", TRUE);
	smarty_data->highlight_matching_tag = utils_get_setting_boolean(config, "geany-smarty", "highlight_matching_tag", TRUE);
	smarty_data->highlight_color = utils_get_setting_boolean(config, "geany-smarty", "highlight_color", 0x00ff00);//green
	smarty_data->smart_indent = utils_get_setting_boolean(config, "geany-smarty", "smart_indent", TRUE);
	smarty_data->smart_close_bracket = utils_get_setting_boolean(config, "geany-smarty", "smart_close_bracket", TRUE);
	
	g_key_file_free(config);
}

static void on_configure_response(GtkDialog *dialog, gint response, gpointer user_data){
	/* catch OK or Apply clicked */
	if (response == GTK_RESPONSE_OK || response == GTK_RESPONSE_APPLY){
		GKeyFile *config = g_key_file_new();
		gchar *data;
		gchar *config_dir = g_path_get_dirname(smarty_data->config_file);
		
		smarty_data->smart_auto_close_xml_tag = (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(
			g_object_get_data(G_OBJECT(dialog), "check_auto_close_tag"))));
		g_key_file_set_boolean(config, "geany-smarty", "smart_auto_close_xml_tag", smarty_data->smart_auto_close_xml_tag);
		
		smarty_data->highlight_matching_tag = (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(
			g_object_get_data(G_OBJECT(dialog), "check_highlight_matching_tag"))));
		g_key_file_set_boolean(config, "geany-smarty", "highlight_matching_tag", smarty_data->highlight_matching_tag);
		g_key_file_set_integer(config, "geany-smarty", "highlight_color", 0x00ff00);
		
		smarty_data->smart_indent = (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(
			g_object_get_data(G_OBJECT(dialog), "check_smart_indent"))));
		g_key_file_set_boolean(config, "geany-smarty", "smart_indent", smarty_data->smart_indent);
		
		smarty_data->smart_close_bracket = (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(
			g_object_get_data(G_OBJECT(dialog), "check_smart_close_bracket"))));
		g_key_file_set_boolean(config, "geany-smarty", "smart_close_bracket", smarty_data->smart_close_bracket);
		
		if (!g_file_test(config_dir, G_FILE_TEST_IS_DIR) && utils_mkdir(config_dir, TRUE) != 0){
			dialogs_show_msgbox(GTK_MESSAGE_ERROR,
				_("Geany Smarty Plugin configuration directory could not be created!"));
		}else{
			/* write config to file */
			data = g_key_file_to_data(config, NULL, NULL);
			utils_write_file(smarty_data->config_file, data);
			g_free(data);
		}
		g_free(config_dir);
		g_key_file_free(config);
	}
}

GtkWidget *plugin_configure(GtkDialog *dialog){
	GtkWidget *vbox;
	GtkWidget *check_auto_close_tag, *check_highlight_matching_tag, *check_smart_indent, *check_smart_close_bracket;
	
	vbox = gtk_vbox_new(FALSE, 0);
	
	check_auto_close_tag = gtk_check_button_new_with_label(_("Smart auto close XML/HTML tag"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_auto_close_tag), smarty_data->smart_auto_close_xml_tag);
	gtk_box_pack_start(GTK_BOX(vbox), check_auto_close_tag, FALSE, FALSE, 3);
	g_object_set_data(G_OBJECT(dialog), "check_auto_close_tag", check_auto_close_tag);
	
	check_highlight_matching_tag = gtk_check_button_new_with_label(_("Highlight XML/HTML tag"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_highlight_matching_tag), smarty_data->highlight_matching_tag);
	gtk_box_pack_start(GTK_BOX(vbox), check_highlight_matching_tag, FALSE, FALSE, 3);
	g_object_set_data(G_OBJECT(dialog), "check_highlight_matching_tag", check_highlight_matching_tag);
	
	check_smart_indent = gtk_check_button_new_with_label(_("Smart auto indent code"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_smart_indent), smarty_data->smart_indent);
	gtk_box_pack_start(GTK_BOX(vbox), check_smart_indent, FALSE, FALSE, 3);
	g_object_set_data(G_OBJECT(dialog), "check_smart_indent", check_smart_indent);
	
	check_smart_close_bracket = gtk_check_button_new_with_label(_("Smart brackets, quoctes close and delete"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_smart_close_bracket), smarty_data->smart_close_bracket);
	gtk_box_pack_start(GTK_BOX(vbox), check_smart_close_bracket, FALSE, FALSE, 3);
	g_object_set_data(G_OBJECT(dialog), "check_smart_close_bracket", check_smart_close_bracket);
	
	gtk_widget_show_all(vbox);

	g_signal_connect(dialog, "response", G_CALLBACK(on_configure_response), NULL);
	return vbox;
}

void plugin_cleanup(void){
	g_free(smarty_data->config_file);
	g_free(smarty_data);
}

void plugin_help(void){
	utils_open_browser("https://github.com/JLamp07/geany-smarty");
}
