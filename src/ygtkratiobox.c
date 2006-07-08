/* YGtkRatioBox container */

#include "ygtkratiobox.h"

static void ygtk_ratio_box_class_init (YGtkRatioBoxClass *klass);
static void ygtk_ratio_box_init       (YGtkRatioBox      *box);
static void ygtk_ratio_box_add        (GtkContainer   *container,
                                  GtkWidget      *widget);
static void ygtk_ratio_box_remove     (GtkContainer   *container,
                                  GtkWidget      *widget);
static void ygtk_ratio_box_forall     (GtkContainer   *container,
                                  gboolean	include_internals,
                                  GtkCallback     callback,
                                  gpointer        callback_data);
static GType ygtk_ratio_box_child_type (GtkContainer   *container);
//static gboolean ygtk_ratio_box_visibility_notify_event (GtkWidget	     *widget,
//                                                GdkEventVisibility  *event);
static void ygtk_ratio_box_size_request  (GtkWidget      *widget,
                                     GtkRequisition *requisition);
static void ygtk_ratio_box_size_allocate (GtkWidget      *widget,
                                     GtkAllocation  *allocation);

//static void ygtk_ratio_box_show_widget (GtkWidget *widget, YGtkRatioBox* box);
//static void ygtk_ratio_box_hide_widget (GtkWidget *widget, YGtkRatioBox* box);

static GtkContainerClass *parent_class = NULL;

GType ygtk_ratio_box_get_type()
{
	static GType box_type = 0;
	if (!box_type) {
		static const GTypeInfo box_info = {
			sizeof (YGtkRatioBoxClass),
			NULL, NULL, (GClassInitFunc) ygtk_ratio_box_class_init, NULL, NULL,
			sizeof (YGtkRatioBox), 0, (GInstanceInitFunc) ygtk_ratio_box_init, NULL
		};

		box_type = g_type_register_static (GTK_TYPE_CONTAINER, "YGtkRatioBox",
		                                   &box_info, (GTypeFlags) 0);
	}
	return box_type;
}

static void ygtk_ratio_box_class_init (YGtkRatioBoxClass *klass)
{
	parent_class = (GtkContainerClass*) g_type_class_peek_parent (klass);

	GtkContainerClass *container_class = GTK_CONTAINER_CLASS (klass);
	container_class->add = ygtk_ratio_box_add;
	container_class->remove = ygtk_ratio_box_remove;
  container_class->forall = ygtk_ratio_box_forall;
  container_class->child_type = ygtk_ratio_box_child_type;

	GtkWidgetClass* widget_class = GTK_WIDGET_CLASS (klass);
	widget_class->size_request  = ygtk_ratio_box_size_request;
	widget_class->size_allocate = ygtk_ratio_box_size_allocate;
}

static void ygtk_ratio_box_init (YGtkRatioBox *box)
{
	GTK_WIDGET_SET_FLAGS (box, GTK_NO_WINDOW);
	gtk_widget_set_redraw_on_allocate (GTK_WIDGET (box), FALSE);

	box->children = NULL;
	box->orientation = box->spacing = 0;
	box->ratios_sum = 0;
}

GtkWidget* ygtk_ratio_box_new (YGtkRatioBoxOrientation orientation, gint spacing)
{
	YGtkRatioBox* box = (YGtkRatioBox*) g_object_new (YGTK_TYPE_RATIO_BOX, NULL);
	YGTK_RATIO_BOX (box)->orientation = orientation;
	YGTK_RATIO_BOX (box)->spacing = spacing;
	return GTK_WIDGET (box);
}

static GType ygtk_ratio_box_child_type (GtkContainer* container)
{ return GTK_TYPE_WIDGET; }

void ygtk_ratio_box_set_child_packing (YGtkRatioBox *box, GtkWidget *child,
                                  gfloat ratio, gboolean fill,
                                  guint padding)
{
	YGtkRatioBoxChild *child_info = NULL;

	g_return_if_fail (IS_YGTK_RATIO_BOX (box));
	g_return_if_fail (GTK_IS_WIDGET (child));

	GList* list;
	for (list = box->children; list; list = list->next) {
		child_info = (YGtkRatioBoxChild*) list->data;
		if (child_info->widget == child)
			break;
	}

	gtk_widget_freeze_child_notify (child);
	if (list) {
		box->ratios_sum -= child_info->ratio;

		child_info->ratio = ratio;
		child_info->fill = fill;
		child_info->padding = padding;

		box->ratios_sum += child_info->ratio;

		if (GTK_WIDGET_VISIBLE (child) && GTK_WIDGET_VISIBLE (box))
			gtk_widget_queue_resize (child);
	}
	gtk_widget_thaw_child_notify (child);
}

void ygtk_ratio_box_set_spacing (YGtkRatioBox *box, gint spacing)
{
	box->spacing = spacing;
}

gint ygtk_ratio_box_get_spacing (YGtkRatioBox *box)
{
	return box->spacing;
}

static void ygtk_ratio_box_add (GtkContainer *container, GtkWidget *child)
{
	YGtkRatioBox* box = YGTK_RATIO_BOX (container);
	YGtkRatioBoxChild* child_info;

	g_return_if_fail (IS_YGTK_RATIO_BOX (box));
	g_return_if_fail (GTK_IS_WIDGET (child));
	g_return_if_fail (child->parent == NULL);

	child_info = g_new (YGtkRatioBoxChild, 1);
	child_info->widget = child;
	child_info->ratio = 1.0;
	child_info->padding = 0;
	child_info->fill = TRUE;
/*
	g_signal_connect (G_OBJECT (child), "show",
	                  G_CALLBACK (ygtk_ratio_box_show_widget), box);
	g_signal_connect (G_OBJECT (child), "hide",
	                  G_CALLBACK (ygtk_ratio_box_hide_widget), box);
*/
	box->ratios_sum += child_info->ratio;

	box->children = g_list_append (box->children, child_info);

	gtk_widget_freeze_child_notify (child);
	gtk_widget_set_parent (child, GTK_WIDGET (box));
	gtk_widget_thaw_child_notify (child);
}

static void ygtk_ratio_box_remove (GtkContainer *container, GtkWidget *widget)
{
	YGtkRatioBox* box = YGTK_RATIO_BOX (container);

	GList* child = box->children;
	for (child = box->children; child; child = child->next) {
		YGtkRatioBoxChild *box_child = (YGtkRatioBoxChild*) child->data;
		if (box_child->widget == widget) {
			box->ratios_sum -= box_child->ratio;

			gboolean was_visible = GTK_WIDGET_VISIBLE (widget);
			gtk_widget_unparent (widget);

			box->children = g_list_remove_link (box->children, child);
			g_list_free (child);
			g_free (box_child);

			if (was_visible)
				gtk_widget_queue_resize (GTK_WIDGET (container));
			break;
		}
	}
}

static void ygtk_ratio_box_forall (GtkContainer *container,
                              gboolean      include_internals,
                              GtkCallback   callback,
                              gpointer      callback_data)
{
	g_return_if_fail (callback != NULL);

	YGtkRatioBox* box = YGTK_RATIO_BOX (container);

	GList* children = box->children;
	while (children) {
		YGtkRatioBoxChild* child = (YGtkRatioBoxChild*) children->data;
		children = children->next;
		(* callback) (child->widget, callback_data);
	}
}

#if 0
static void ygtk_ratio_box_show_widget (GtkWidget *widget, YGtkRatioBox* box)
{
	GList* child = box->children;
	for (child = box->children; child; child = child->next) {
		YGtkRatioBoxChild *box_child = (YGtkRatioBoxChild*) child->data;
		if (box_child->widget == widget)
			box->ratios_sum += box_child->ratio;
	}
}


static void ygtk_ratio_box_hide_widget (GtkWidget *widget, YGtkRatioBox* box)
{
	GList* child = box->children;
	for (child = box->children; child; child = child->next) {
		YGtkRatioBoxChild *box_child = (YGtkRatioBoxChild*) child->data;
		if (box_child->widget == widget)
			box->ratios_sum -= box_child->ratio;
	}
}
#endif
/*
static gboolean ygtk_ratio_box_visibility_notify_event (GtkWidget          *widget,
                                                   GdkEventVisibility *event)
{
	YGtkRatioBox* box = YGTK_RATIO_BOX (widget);
printf("** widget set (in)visible\n");
	GList* child = box->children;
	for (child = box->children; child; child = child->next) {
		YGtkRatioBoxChild* box_child = (YGtkRatioBoxChild*) child->data;
		if (box_child->widget == widget) {
			if (GTK_WIDGET_VISIBLE (widget))
				box->ratios_sum += box_child->ratio;
			else
				box->ratios_sum -= box_child->ratio;
		}
	}
	return FALSE;
}
*/
static void ygtk_ratio_box_size_request (GtkWidget      *widget,
                                    GtkRequisition *requisition)
{
	guint box_length = GTK_CONTAINER (widget)->border_width * 2;
	guint box_height = 0;
	YGtkRatioBox* box = YGTK_RATIO_BOX (widget);

	gfloat pixels_per_percent = 0;

	GList* child;
	for (child = g_list_first (box->children); child; child = child->next) {
		YGtkRatioBoxChild* box_child = (YGtkRatioBoxChild*) child->data;
		if (GTK_WIDGET_VISIBLE (box_child->widget)) {
			GtkRequisition widget_requisition;
			gtk_widget_size_request (box_child->widget, &widget_requisition);

			int widget_length, widget_height;
			if (box->orientation == YGTK_RATIO_BOX_HORIZONTAL_ORIENTATION) {
				widget_length = widget_requisition.width;
				widget_height = widget_requisition.height;
			}
			else { // orientation == YGTK_RATIO_BOX_VERTICAL_ORIENTATION
				widget_length = widget_requisition.height;
				widget_height = widget_requisition.width;
			}
			widget_length += box_child->padding*2 + box->spacing*2;

			if (box_child->ratio) {
				gfloat percent_of_whole = box_child->ratio / box->ratios_sum * 100;
				pixels_per_percent = MAX (widget_length / percent_of_whole,
				                          pixels_per_percent);
			}
			else
				box_length += widget_length;

			box_height = MAX (box_height, widget_height +
			                  GTK_CONTAINER (widget)->border_width*2);
		}
	}

	// The space for the proportional containees
	box_length += (guint) (pixels_per_percent * 100);

	if (box->orientation == YGTK_RATIO_BOX_HORIZONTAL_ORIENTATION) {
		requisition->width = box_length;
		requisition->height = box_height;
	}
	else { // orientation == YGTK_RATIO_BOX_VERTICAL_ORIENTATION
		requisition->height = box_length;
		requisition->width  = box_height;
	}
}

static void ygtk_ratio_box_size_allocate (GtkWidget     *widget,
                                     GtkAllocation *allocation)
{
// TODO: honor this:
// * text direction - gtk_widget_get_direction (widget);
// * visibility - GTK_WIDGET_VISIBLE (child->widget)
	GList* child;
	int box_length;
	YGtkRatioBox* box = YGTK_RATIO_BOX (widget);

	// Calculate actual size for the exansable widgets
	if (box->orientation == YGTK_RATIO_BOX_HORIZONTAL_ORIENTATION)
		box_length = allocation->width;
	else  // YGTK_RATIO_BOX_VERTICAL_ORIENTATION
		box_length = allocation->height;

	box_length -= GTK_CONTAINER (box)->border_width * 2;

	for (child = g_list_first (box->children); child; child = child->next) {
		YGtkRatioBoxChild* box_child = (YGtkRatioBoxChild*) child->data;
		if (box_child->ratio == 0) {
			GtkRequisition requisition;
			gtk_widget_size_request (box_child->widget, &requisition);

			if (box->orientation == YGTK_RATIO_BOX_HORIZONTAL_ORIENTATION)
				box_length -= requisition.width;
			else  // YGTK_RATIO_BOX_VERTICAL_ORIENTATION
				box_length -= requisition.height;
		}
		box_length -= box_child->padding*2 + box->spacing*2;
	}

	gfloat child_pos = 0;
	for (child = g_list_first (box->children); child; child = child->next) {
		GtkAllocation child_allocation;
		YGtkRatioBoxChild* box_child = (YGtkRatioBoxChild*) child->data;

		if (!GTK_WIDGET_VISIBLE (box_child->widget))
			continue;

		GtkRequisition requisition;
		gtk_widget_size_request (box_child->widget, &requisition);

		// ratio 0 == non-expansible
		if (box_child->ratio == 0) {
			if (box->orientation == YGTK_RATIO_BOX_HORIZONTAL_ORIENTATION) {
				child_allocation.x = allocation->x + (gint)child_pos;
				child_allocation.y = allocation->y;
				child_allocation.width  = requisition.width;
				child_allocation.height = allocation->height;
				child_pos += child_allocation.width;
			}
			else {  // YGTK_RATIO_BOX_VERTICAL_ORIENTATION
				child_allocation.x = allocation->x;
				child_allocation.y = allocation->y + (gint)child_pos;
				child_allocation.width  = allocation->width;
				child_allocation.height = requisition.height;
				child_pos += child_allocation.height;
			}
		}
		// expansible
		else {
			gfloat length = (box_child->ratio / box->ratios_sum) * box_length;

			if (box->orientation == YGTK_RATIO_BOX_HORIZONTAL_ORIENTATION) {
				child_allocation.x = allocation->x + (gint)child_pos;
				child_allocation.y = allocation->y;
				child_allocation.width = (gint)length;
				child_allocation.height = allocation->height;
			}
			else {  // YGTK_RATIO_BOX_VERTICAL_ORIENTATION
				child_allocation.x = allocation->x;
				child_allocation.y = allocation->y + (gint)child_pos;
				child_allocation.width = allocation->width;
				child_allocation.height = (gint)length;
			}
			child_pos += length;
		}

		if (box->orientation == YGTK_RATIO_BOX_HORIZONTAL_ORIENTATION) {
			child_allocation.x += box->spacing + box_child->padding;
			child_pos += box->spacing * 2 + box_child->padding * 2;
		}
		else {  // YGTK_RATIO_BOX_VERTICAL_ORIENTATION
			child_allocation.y += box->spacing + box_child->padding;
			child_pos += box->spacing * 2 + box_child->padding * 2;
		}

		if (!box_child->fill) {
			child_allocation.width  = MIN (child_allocation.width, requisition.width);
			child_allocation.height = MIN (child_allocation.height, requisition.height);
		}

		gtk_widget_size_allocate (box_child->widget, &child_allocation);
	}
}