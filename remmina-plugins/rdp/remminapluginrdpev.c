/*
 * Remmina - The GTK+ Remote Desktop Client
 * Copyright (C) 2010 Jay Sorg
 * Copyright (C) 2010-2011 Vic Lee
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, 
 * Boston, MA 02111-1307, USA.
 */

#include "remminapluginrdp.h"
#include "remminapluginrdpev.h"
#include "remminapluginrdpui.h"
#include <gdk/gdkkeysyms.h>
#include <freerdp/kbd.h>

static void
remmina_plugin_rdpev_event_push (RemminaProtocolWidget *gp,
    gint type, gint flag, gint param1, gint param2)
{
    RemminaPluginRdpData *gpdata;
    RemminaPluginRdpEvent *event;

    gpdata = GET_DATA (gp);
    event = g_new (RemminaPluginRdpEvent, 1);
    event->type = type;
    event->flag = flag;
    event->param1 = param1;
    event->param2 = param2;
    if (gpdata->event_queue)
    {
        g_async_queue_push (gpdata->event_queue, event);
        (void) write (gpdata->event_pipe[1], "\0", 1);
    }
}

static void
remmina_plugin_rdpev_release_key (RemminaProtocolWidget *gp, gint scancode)
{
    RemminaPluginRdpData *gpdata;
    gint k;
    gint i;

    gpdata = GET_DATA (gp);
    if (scancode == 0)
    {
        /* Send all release key events for previously pressed keys */
        for (i = 0; i < gpdata->pressed_keys->len; i++)
        {
            k = g_array_index (gpdata->pressed_keys, gint, i);
            remmina_plugin_rdpev_event_push (gp, RDP_INPUT_SCANCODE, RDP_KEYRELEASE, k, 0);
        }
        g_array_set_size (gpdata->pressed_keys, 0);
    }
    else
    {
        /* Unregister the keycode only */
        for (i = 0; i < gpdata->pressed_keys->len; i++)
        {
            k = g_array_index (gpdata->pressed_keys, gint, i);
            if (k == scancode)
            {
                g_array_remove_index_fast (gpdata->pressed_keys, i);
                break;
            }
        }
    }
}

static void
remmina_plugin_rdpev_scale_area (RemminaProtocolWidget *gp, gint *x, gint *y, gint *w, gint *h)
{
    RemminaPluginRdpData *gpdata;
    gint sx, sy, sw, sh;
    gint width, height;

    gpdata = GET_DATA (gp);
    if (!gpdata->rgb_surface) return;

    width = remmina_plugin_service->protocol_plugin_get_width (gp);
    height = remmina_plugin_service->protocol_plugin_get_height (gp);
    if (width == 0 || height == 0) return;

    if (gpdata->scale_width == width && gpdata->scale_height == height)
    {
        /* Same size, just copy the pixels */
        *x = MIN (MAX (0, *x), width - 1);
        *y = MIN (MAX (0, *y), height - 1);
        *w = MIN (width - *x, *w);
        *h = MIN (height - *y, *h);
        return;
    }

    /* We have to extend the scaled region one scaled pixel, to avoid gaps */
    sx = MIN (MAX (0, (*x) * gpdata->scale_width / width
        - gpdata->scale_width / width - 2), gpdata->scale_width - 1);
    sy = MIN (MAX (0, (*y) * gpdata->scale_height / height
        - gpdata->scale_height / height - 2), gpdata->scale_height - 1);
    sw = MIN (gpdata->scale_width - sx, (*w) * gpdata->scale_width / width
        + gpdata->scale_width / width + 4);
    sh = MIN (gpdata->scale_height - sy, (*h) * gpdata->scale_height / height
        + gpdata->scale_height / height + 4);

    *x = sx; *y = sy; *w = sw; *h = sh;
}

static void
remmina_plugin_rdpev_update_rect (RemminaProtocolWidget *gp, gint x, gint y, gint w, gint h)
{
    RemminaPluginRdpData *gpdata;

    gpdata = GET_DATA (gp);
    if (remmina_plugin_service->protocol_plugin_get_scale (gp))
    {
        remmina_plugin_rdpev_scale_area (gp, &x, &y, &w, &h);
    }
    gtk_widget_queue_draw_area (gpdata->drawing_area, x, y, w, h);
}

static gboolean
remmina_plugin_rdpev_update_scale_factor (RemminaProtocolWidget *gp)
{
    RemminaPluginRdpData *gpdata;
    RemminaFile *remminafile;
    gint width, height;
    gint gpwidth, gpheight;
    gboolean scale;
    gint hscale, vscale;

    gpdata = GET_DATA (gp);
    remminafile = remmina_plugin_service->protocol_plugin_get_file (gp);

    width = GTK_WIDGET (gp)->allocation.width;
    height = GTK_WIDGET (gp)->allocation.height;
    scale = remmina_plugin_service->protocol_plugin_get_scale (gp);
    if (scale)
    {
        if (width > 1 && height > 1)
        {
            gpwidth = remmina_plugin_service->protocol_plugin_get_width (gp);
            gpheight = remmina_plugin_service->protocol_plugin_get_height (gp);
            hscale = remmina_plugin_service->file_get_int (remminafile, "hscale", 0);
            vscale = remmina_plugin_service->file_get_int (remminafile, "vscale", 0);
            gpdata->scale_width = (hscale > 0 ?
                MAX (1, gpwidth * hscale / 100) : width);
            gpdata->scale_height = (vscale > 0 ?
                MAX (1, gpheight * vscale / 100) : height);

            gpdata->scale_x = (gdouble)gpdata->scale_width / (gdouble)gpwidth;
            gpdata->scale_y = (gdouble)gpdata->scale_height / (gdouble)gpheight;
        }
    }
    else
    {
        gpdata->scale_width = 0;
        gpdata->scale_height = 0;
        gpdata->scale_x = 0;
        gpdata->scale_y = 0;
    }
    if (width > 1 && height > 1)
    {
        gtk_widget_queue_draw_area (GTK_WIDGET (gp), 0, 0, width, height);
    }
    gpdata->scale_handler = 0;
    return FALSE;
}

static gboolean
remmina_plugin_rdpev_on_expose (GtkWidget *widget, GdkEventExpose *event, RemminaProtocolWidget *gp)
{
    RemminaPluginRdpData *gpdata;
    gint x, y;
    gboolean scale;
    cairo_t *context;

    gpdata = GET_DATA (gp);

    if (!gpdata->rgb_pixmap) return FALSE;

    scale = remmina_plugin_service->protocol_plugin_get_scale (gp);
    x = event->area.x;
    y = event->area.y;

    context = gdk_cairo_create (GDK_DRAWABLE (gpdata->drawing_area->window));
    cairo_rectangle (context, x, y, event->area.width, event->area.height);
    if (scale)
    {
        cairo_scale (context, gpdata->scale_x, gpdata->scale_y);
    }
    gdk_cairo_set_source_pixmap (context, gpdata->rgb_pixmap, 0, 0);
    cairo_fill (context);
    cairo_destroy (context);
    return TRUE;
}

static gboolean
remmina_plugin_rdpev_on_configure (GtkWidget *widget, GdkEventConfigure *event, RemminaProtocolWidget *gp)
{
    RemminaPluginRdpData *gpdata;

    gpdata = GET_DATA (gp);
    /* We do a delayed reallocating to improve performance */
    if (gpdata->scale_handler) g_source_remove (gpdata->scale_handler);
    gpdata->scale_handler = g_timeout_add (1000, (GSourceFunc) remmina_plugin_rdpev_update_scale_factor, gp);
    return FALSE;
}

static void
remmina_plugin_rdpev_translate_pos (RemminaProtocolWidget *gp, int ix, int iy, int *ox, int *oy)
{
    RemminaPluginRdpData *gpdata;

    gpdata = GET_DATA (gp);
    if (gpdata->scale && gpdata->scale_width >= 1 && gpdata->scale_height >= 1)
    {
        *ox = ix * remmina_plugin_service->protocol_plugin_get_width (gp) / gpdata->scale_width;
        *oy = iy * remmina_plugin_service->protocol_plugin_get_height (gp) / gpdata->scale_height;
    }
    else
    {
        *ox = ix;
        *oy = iy;
    }
}

static gboolean
remmina_plugin_rdpev_on_motion (GtkWidget *widget, GdkEventMotion *event, RemminaProtocolWidget *gp)
{
    gint x, y;

    remmina_plugin_rdpev_translate_pos (gp, event->x, event->y, &x, &y);
    remmina_plugin_rdpev_event_push (gp, RDP_INPUT_MOUSE, PTRFLAGS_MOVE, x, y);
    return TRUE;
}

static gboolean
remmina_plugin_rdpev_on_button (GtkWidget *widget, GdkEventButton *event, RemminaProtocolWidget *gp)
{
    gint x, y;
    gint flag;

    /* We only accept 3 buttons */
    if (event->button < 1 || event->button > 3) return FALSE;
    /* We bypass 2button-press and 3button-press events */
    if (event->type != GDK_BUTTON_PRESS && event->type != GDK_BUTTON_RELEASE) return TRUE;

    remmina_plugin_rdpev_translate_pos (gp, event->x, event->y, &x, &y);

    flag = 0;
    if (event->type == GDK_BUTTON_PRESS)
    {
        flag = PTRFLAGS_DOWN;
    }
    switch (event->button)
    {
        case 1:
            flag |= PTRFLAGS_BUTTON1;
            break;
        case 2:
            flag |= PTRFLAGS_BUTTON3;
            break;
        case 3:
            flag |= PTRFLAGS_BUTTON2;
            break;
    }
    if (flag != 0)
    {
        remmina_plugin_rdpev_event_push (gp, RDP_INPUT_MOUSE, flag, x, y);
    }
    return TRUE;
}

static gboolean
remmina_plugin_rdpev_on_scroll (GtkWidget *widget, GdkEventScroll *event, RemminaProtocolWidget *gp)
{
    gint x, y;
    gint flag;

    flag = 0;
    switch (event->direction)
    {
    case GDK_SCROLL_UP:
        flag = PTRFLAGS_WHEEL | 0x0078;
        break;
    case GDK_SCROLL_DOWN:
        flag = PTRFLAGS_WHEEL | PTRFLAGS_WHEEL_NEGATIVE | 0x0088;
        break;
    default:
        return FALSE;
    }

    remmina_plugin_rdpev_translate_pos (gp, event->x, event->y, &x, &y);
    remmina_plugin_rdpev_event_push (gp, RDP_INPUT_MOUSE, flag, x, y);
    return TRUE;
}

static gboolean
remmina_plugin_rdpev_on_key (GtkWidget *widget, GdkEventKey *event, RemminaProtocolWidget *gp)
{
    RemminaPluginRdpData *gpdata;
    gint flag;
    gint scancode = 0;
    Display *display;
    KeyCode cooked_keycode;

    gpdata = GET_DATA (gp);
    flag = (event->type == GDK_KEY_PRESS ? RDP_KEYPRESS : RDP_KEYRELEASE);
    switch (event->keyval)
    {
    case GDK_Break:
        remmina_plugin_rdpev_event_push (gp, RDP_INPUT_SCANCODE, flag, 0xc6, 0);
        break;
    case GDK_Pause:
        remmina_plugin_rdpev_event_push (gp, RDP_INPUT_SCANCODE, flag | 0x0200, 0x1d, 0);
        remmina_plugin_rdpev_event_push (gp, RDP_INPUT_SCANCODE, flag, 0x45, 0);
        break;
    default:
        if (!gpdata->use_client_keymap)
        {
            scancode = freerdp_kbd_get_scancode_by_keycode (event->hardware_keycode, &flag);
            remmina_plugin_service->log_printf ("[RDP]keyval=%04X keycode=%i scancode=%i flag=%04X\n",
                event->keyval, event->hardware_keycode, scancode, flag);
        }
        else
        {
            display = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
            cooked_keycode = XKeysymToKeycode(display, XKeycodeToKeysym(display, event->hardware_keycode, 0));
            scancode = freerdp_kbd_get_scancode_by_keycode (cooked_keycode, &flag);
            remmina_plugin_service->log_printf ("[RDP]keyval=%04X raw_keycode=%i cooked_keycode=%i scancode=%i flag=%04X\n",
                event->keyval, event->hardware_keycode, cooked_keycode, scancode, flag);
        }
        if (scancode)
        {
            remmina_plugin_rdpev_event_push (gp, RDP_INPUT_SCANCODE, flag, scancode, 0);
        }
        break;
    }

    /* Register/unregister the pressed key */
    if (scancode)
    {
        if (event->type == GDK_KEY_PRESS)
        {
            g_array_append_val (gpdata->pressed_keys, scancode);
        }
        else
        {
            remmina_plugin_rdpev_release_key (gp, scancode);
        }
    }
    return TRUE;
}

void
remmina_plugin_rdpev_init (RemminaProtocolWidget *gp)
{
    RemminaPluginRdpData *gpdata;
    gint flags;
    gchar *s;
    XPixmapFormatValues *pfs;
    XPixmapFormatValues *pf;
    gint n;
    gint i;

    gpdata = GET_DATA (gp);
    gpdata->drawing_area = gtk_drawing_area_new ();
    gtk_widget_show (gpdata->drawing_area);
    gtk_container_add (GTK_CONTAINER (gp), gpdata->drawing_area);

    gtk_widget_add_events (gpdata->drawing_area, GDK_POINTER_MOTION_MASK | GDK_BUTTON_PRESS_MASK
        | GDK_BUTTON_RELEASE_MASK | GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK);
    GTK_WIDGET_SET_FLAGS (gpdata->drawing_area, GTK_CAN_FOCUS);

    remmina_plugin_service->protocol_plugin_register_hostkey (gp, gpdata->drawing_area);

    s = remmina_plugin_service->pref_get_value ("rdp_use_client_keymap");
    gpdata->use_client_keymap = (s && s[0] == '1' ? TRUE : FALSE);
    g_free (s);

    g_signal_connect (G_OBJECT (gpdata->drawing_area), "expose_event",
        G_CALLBACK (remmina_plugin_rdpev_on_expose), gp);
    g_signal_connect (G_OBJECT (gpdata->drawing_area), "configure_event",
        G_CALLBACK (remmina_plugin_rdpev_on_configure), gp);
    g_signal_connect (G_OBJECT (gpdata->drawing_area), "motion-notify-event",
        G_CALLBACK (remmina_plugin_rdpev_on_motion), gp);
    g_signal_connect (G_OBJECT (gpdata->drawing_area), "button-press-event",
        G_CALLBACK (remmina_plugin_rdpev_on_button), gp);
    g_signal_connect (G_OBJECT (gpdata->drawing_area), "button-release-event",
        G_CALLBACK (remmina_plugin_rdpev_on_button), gp);
    g_signal_connect (G_OBJECT (gpdata->drawing_area), "scroll-event",
        G_CALLBACK (remmina_plugin_rdpev_on_scroll), gp);
    g_signal_connect (G_OBJECT (gpdata->drawing_area), "key-press-event",
        G_CALLBACK (remmina_plugin_rdpev_on_key), gp);
    g_signal_connect (G_OBJECT (gpdata->drawing_area), "key-release-event",
        G_CALLBACK (remmina_plugin_rdpev_on_key), gp);

    gpdata->pressed_keys = g_array_new (FALSE, TRUE, sizeof (gint));
    gpdata->event_queue = g_async_queue_new_full (g_free);
    gpdata->ui_queue = g_async_queue_new_full (remmina_plugin_rdpui_object_free);
    if (pipe (gpdata->event_pipe))
    {
        g_print ("Error creating pipes.\n");
        gpdata->event_pipe[0] = -1;
        gpdata->event_pipe[1] = -1;
    }
    else
    {
        flags = fcntl (gpdata->event_pipe[0], F_GETFL, 0);
        fcntl (gpdata->event_pipe[0], F_SETFL, flags | O_NONBLOCK);
    }

    gpdata->object_table = g_hash_table_new (NULL, NULL);

    gpdata->display = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
    gpdata->depth = DefaultDepth (gpdata->display, DefaultScreen (gpdata->display));
    gpdata->visual = GDK_VISUAL_XVISUAL (gdk_visual_get_best_with_depth (gpdata->depth));
    pfs = XListPixmapFormats (gpdata->display, &n);
    if (pfs)
    {
        for (i = 0; i < n; i++)
        {
            pf = pfs + i;
            if (pf->depth == gpdata->depth)
            {
                gpdata->bitmap_pad = pf->scanline_pad;
                gpdata->bpp = pf->bits_per_pixel;
                break;
            }
        }
        XFree(pfs);
    }
}

void
remmina_plugin_rdpev_pre_connect (RemminaProtocolWidget *gp)
{
}

void
remmina_plugin_rdpev_post_connect (RemminaProtocolWidget *gp)
{
}

void
remmina_plugin_rdpev_uninit (RemminaProtocolWidget *gp)
{
    RemminaPluginRdpData *gpdata;

    gpdata = GET_DATA (gp);
    if (gpdata->scale_handler)
    {
        g_source_remove (gpdata->scale_handler);
        gpdata->scale_handler = 0;
    }
    if (gpdata->ui_handler)
    {
        g_source_remove (gpdata->ui_handler);
        gpdata->ui_handler = 0;
        remmina_plugin_rdpev_queue_ui (gp);
    }

    if (gpdata->gc)
    {
        XFreeGC (gpdata->display, gpdata->gc);
        gpdata->gc = 0;
    }
    if (gpdata->rgb_pixmap)
    {
        g_object_unref (gpdata->rgb_pixmap);
        gpdata->rgb_pixmap = NULL;
    }

    g_hash_table_destroy (gpdata->object_table);

    g_array_free (gpdata->pressed_keys, TRUE);
    g_async_queue_unref (gpdata->event_queue);
    gpdata->event_queue = NULL;
    g_async_queue_unref (gpdata->ui_queue);
    gpdata->ui_queue = NULL;
    close (gpdata->event_pipe[0]);
    close (gpdata->event_pipe[1]);
}

void
remmina_plugin_rdpev_update_scale (RemminaProtocolWidget *gp)
{
    RemminaPluginRdpData *gpdata;
    RemminaFile *remminafile;
    gint width, height;
    gint hscale, vscale;

    gpdata = GET_DATA (gp);
    remminafile = remmina_plugin_service->protocol_plugin_get_file (gp);

    width = remmina_plugin_service->protocol_plugin_get_width (gp);
    height = remmina_plugin_service->protocol_plugin_get_height (gp);
    hscale = remmina_plugin_service->file_get_int (remminafile, "hscale", 0);
    vscale = remmina_plugin_service->file_get_int (remminafile, "vscale", 0);
    if (gpdata->scale)
    {
        gtk_widget_set_size_request (GTK_WIDGET (gpdata->drawing_area),
            (hscale > 0 ? width * hscale / 100 : -1),
            (vscale > 0 ? height * vscale / 100 : -1));
    }
    else
    {
        gtk_widget_set_size_request (GTK_WIDGET (gpdata->drawing_area), width, height);
    }
}

static uint8 remmina_plugin_rdpev_rop2_map[] =
{
    GXclear,        /* 0 */
    GXnor,          /* DPon */
    GXandInverted,  /* DPna */
    GXcopyInverted, /* Pn */
    GXandReverse,   /* PDna */
    GXinvert,       /* Dn */
    GXxor,          /* DPx */
    GXnand,         /* DPan */
    GXand,          /* DPa */
    GXequiv,        /* DPxn */
    GXnoop,         /* D */
    GXorInverted,   /* DPno */
    GXcopy,         /* P */
    GXorReverse,    /* PDno */
    GXor,           /* DPo */
    GXset           /* 1 */
};

static void
remmina_plugin_rdpev_set_rop2 (RemminaPluginRdpData *gpdata, gint rop2)
{
    if ((rop2 < 0x01) || (rop2 > 0x10))
    {
        remmina_plugin_service->log_printf ("[RDP]unknown rop2 0x%x", rop2);
    }
    else
    {
        XSetFunction (gpdata->display, gpdata->gc, remmina_plugin_rdpev_rop2_map[rop2 - 1]);
    }
}

static void
remmina_plugin_rdpev_set_rop3 (RemminaPluginRdpData *gpdata, gint rop3)
{
    switch (rop3)
    {
        case 0x00: /* 0 - 0 */
            XSetFunction (gpdata->display, gpdata->gc, GXclear);
            break;
        case 0x05: /* ~(P | D) - DPon */
            XSetFunction (gpdata->display, gpdata->gc, GXnor);
            break;
        case 0x0a: /* ~P & D - DPna */
            XSetFunction (gpdata->display, gpdata->gc, GXandInverted);
            break;
        case 0x0f: /* ~P - Pn */
            XSetFunction (gpdata->display, gpdata->gc, GXcopyInverted);
            break;
        case 0x11: /* ~(S | D) - DSon */
            XSetFunction (gpdata->display, gpdata->gc, GXnor);
            break;
        case 0x22: /* ~S & D - DSna */
            XSetFunction (gpdata->display, gpdata->gc, GXandInverted);
            break;
        case 0x33: /* ~S - Sn */
            XSetFunction (gpdata->display, gpdata->gc, GXcopyInverted);
            break;
        case 0x44: /* S & ~D - SDna */
            XSetFunction (gpdata->display, gpdata->gc, GXandReverse);
            break;
        case 0x50: /* P & ~D - PDna */
            XSetFunction (gpdata->display, gpdata->gc, GXandReverse);
            break;
        case 0x55: /* ~D - Dn */
            XSetFunction (gpdata->display, gpdata->gc, GXinvert);
            break;
        case 0x5a: /* D ^ P - DPx */
            XSetFunction (gpdata->display, gpdata->gc, GXxor);
            break;
        case 0x5f: /* ~(P & D) - DPan */
            XSetFunction (gpdata->display, gpdata->gc, GXnand);
            break;
        case 0x66: /* D ^ S - DSx */
            XSetFunction (gpdata->display, gpdata->gc, GXxor);
            break;
        case 0x77: /* ~(S & D) - DSan */
            XSetFunction (gpdata->display, gpdata->gc, GXnand);
            break;
        case 0x88: /* D & S - DSa */
            XSetFunction (gpdata->display, gpdata->gc, GXand);
            break;
        case 0x99: /* ~(S ^ D) - DSxn */
            XSetFunction (gpdata->display, gpdata->gc, GXequiv);
            break;
        case 0xa0: /* P & D - DPa */
            XSetFunction (gpdata->display, gpdata->gc, GXand);
            break;
        case 0xa5: /* ~(P ^ D) - PDxn */
            XSetFunction (gpdata->display, gpdata->gc, GXequiv);
            break;
        case 0xaa: /* D - D */
            XSetFunction (gpdata->display, gpdata->gc, GXnoop);
            break;
        case 0xaf: /* ~P | D - DPno */
            XSetFunction (gpdata->display, gpdata->gc, GXorInverted);
            break;
        case 0xbb: /* ~S | D - DSno */
            XSetFunction (gpdata->display, gpdata->gc, GXorInverted);
            break;
        case 0xcc: /* S - S */
            XSetFunction (gpdata->display, gpdata->gc, GXcopy);
            break;
        case 0xdd: /* S | ~D - SDno */
            XSetFunction (gpdata->display, gpdata->gc, GXorReverse);
            break;
        case 0xee: /* D | S - DSo */
            XSetFunction (gpdata->display, gpdata->gc, GXor);
            break;
        case 0xf0: /* P - P */
            XSetFunction (gpdata->display, gpdata->gc, GXcopy);
            break;
        case 0xf5: /* P | ~D - PDno */
            XSetFunction (gpdata->display, gpdata->gc, GXorReverse);
            break;
        case 0xfa: /* P | D - DPo */
            XSetFunction (gpdata->display, gpdata->gc, GXor);
            break;
        case 0xff: /* 1 - 1 */
            XSetFunction (gpdata->display, gpdata->gc, GXset);
            break;
        default:
            remmina_plugin_service->log_printf ("[RDP]unknown rop3 0x%x", rop3);
            break;
    }
}

static void
remmina_plugin_rdpev_connected (RemminaProtocolWidget *gp, RemminaPluginRdpUiObject *ui)
{
    RemminaPluginRdpData *gpdata;
    XGCValues gcv = { 0 };

    gpdata = GET_DATA (gp);
    gtk_widget_realize (gpdata->drawing_area);

    gpdata->rgb_pixmap = gdk_pixmap_new (GDK_DRAWABLE (gpdata->drawing_area->window),
        gpdata->settings->width, gpdata->settings->height, gpdata->depth);
    gpdata->rgb_surface = GDK_PIXMAP_XID (gpdata->rgb_pixmap);
    gpdata->drw_surface = gpdata->rgb_surface;
    gpdata->gc = XCreateGC(gpdata->display,
        gpdata->rgb_surface,
        GCGraphicsExposures, &gcv);

    remmina_plugin_rdpev_update_scale (gp);
}

static void
remmina_plugin_rdpev_rect (RemminaProtocolWidget *gp, RemminaPluginRdpUiObject *ui)
{
    RemminaPluginRdpData *gpdata;

    /*g_print ("rect: opcode=%X x=%i y=%i cx=%i cy=%i fgcolor=%X\n", ui->opcode, ui->x, ui->y, ui->cx, ui->cy, ui->fgcolor);*/
    gpdata = GET_DATA (gp);
    remmina_plugin_rdpev_set_rop3 (gpdata, ui->opcode);
    XSetFillStyle (gpdata->display, gpdata->gc, FillSolid);
    XSetForeground (gpdata->display, gpdata->gc, ui->fgcolor);
    XFillRectangle (gpdata->display, gpdata->drw_surface, gpdata->gc, ui->x, ui->y, ui->cx, ui->cy);
    if (gpdata->drw_surface == gpdata->rgb_surface)
    {
        remmina_plugin_rdpev_update_rect (gp, ui->x, ui->y, ui->cx, ui->cy);
    }
}

static void
remmina_plugin_rdpev_create_surface (RemminaProtocolWidget *gp, RemminaPluginRdpUiObject *ui)
{
    RemminaPluginRdpData *gpdata;
    Pixmap new_pix, old_pix;

    /*g_print ("create_surface: object_id=%i old_object_id=%i\n", ui->object_id, ui->alt_object_id);*/
    gpdata = GET_DATA (gp);
    new_pix = XCreatePixmap (gpdata->display, gpdata->rgb_surface, ui->width, ui->height, gpdata->depth);
    g_hash_table_insert (gpdata->object_table, (gpointer) ui->object_id, (gpointer) new_pix);
    if (ui->alt_object_id)
    {
        old_pix = (Pixmap) g_hash_table_lookup (gpdata->object_table, (gpointer) ui->alt_object_id);
        XCopyArea (gpdata->display, old_pix, new_pix, gpdata->gc, 0, 0,
            ui->width, ui->height, 0, 0);
        XFreePixmap (gpdata->display, old_pix);
        if (gpdata->drw_surface == old_pix)
        {
            gpdata->drw_surface = new_pix;
        }
    }
}

static void
remmina_plugin_rdpev_set_surface (RemminaProtocolWidget *gp, RemminaPluginRdpUiObject *ui)
{
    RemminaPluginRdpData *gpdata;

    /*g_print ("set_surface: object_id=%i\n", ui->object_id);*/
    gpdata = GET_DATA (gp);
    if (ui->object_id)
    {
        gpdata->drw_surface = (Drawable) g_hash_table_lookup (gpdata->object_table, (gpointer) ui->object_id);
    }
    else
    {
        gpdata->drw_surface = gpdata->rgb_surface;
    }
}

static void
remmina_plugin_rdpev_destroy_surface (RemminaProtocolWidget *gp, RemminaPluginRdpUiObject *ui)
{
    RemminaPluginRdpData *gpdata;
    Pixmap pix;

    g_print ("destroy_surface: object_id=%i\n", ui->object_id);
    gpdata = GET_DATA (gp);
    pix = (Pixmap) g_hash_table_lookup (gpdata->object_table, (gpointer) ui->object_id);
    if (gpdata->drw_surface == pix)
    {
        gpdata->drw_surface = gpdata->rgb_surface;
    }
    if (pix)
    {
        XFreePixmap (gpdata->display, pix);
    }
    g_hash_table_remove (gpdata->object_table, (gpointer) ui->object_id);
}

static void
remmina_plugin_rdpev_create_bitmap (RemminaProtocolWidget *gp, RemminaPluginRdpUiObject *ui)
{
    RemminaPluginRdpData *gpdata;
    XImage *image;
    Pixmap bitmap;

    g_print ("create_bitmap: object_id=%i\n", ui->object_id);
    gpdata = GET_DATA (gp);
    bitmap = XCreatePixmap (gpdata->display, gpdata->rgb_surface, ui->width, ui->height, gpdata->depth);
    image = XCreateImage (gpdata->display, gpdata->visual, gpdata->depth, ZPixmap, 0,
        (char *) ui->data, ui->width, ui->height, gpdata->bitmap_pad, 0);
    XPutImage (gpdata->display, bitmap, gpdata->gc, image, 0, 0, 0, 0, ui->width, ui->height);
    XFree (image);
    g_hash_table_insert (gpdata->object_table, (gpointer) ui->object_id, (gpointer) bitmap);
}

static void
remmina_plugin_rdpev_paint_bitmap (RemminaProtocolWidget *gp, RemminaPluginRdpUiObject *ui)
{
    RemminaPluginRdpData *gpdata;
    XImage *image;

    g_print ("paint_bitmap: x=%i y=%i cx=%i cy=%i width=%i height=%i\n", ui->x, ui->y, ui->cx, ui->cy, ui->width, ui->height);
    gpdata = GET_DATA (gp);
    image = XCreateImage (gpdata->display, gpdata->visual, gpdata->depth, ZPixmap, 0,
        (char *) ui->data, ui->width, ui->height, gpdata->bitmap_pad, 0);
    XPutImage (gpdata->display, gpdata->rgb_surface, gpdata->gc, image, 0, 0, ui->x, ui->y, ui->cx, ui->cy);
    XFree (image);
    remmina_plugin_rdpev_update_rect (gp, ui->x, ui->y, ui->cx, ui->cy);
}

static void
remmina_plugin_rdpev_destroy_bitmap (RemminaProtocolWidget *gp, RemminaPluginRdpUiObject *ui)
{
    RemminaPluginRdpData *gpdata;
    Pixmap bitmap;

    g_print ("destroy_bitmap: object_id=%i\n", ui->object_id);
    gpdata = GET_DATA (gp);
    bitmap = (Pixmap) g_hash_table_lookup (gpdata->object_table, (gpointer) ui->object_id);
    XFreePixmap (gpdata->display, (Pixmap) bitmap);
    g_hash_table_remove (gpdata->object_table, (gpointer) ui->object_id);
}

gboolean
remmina_plugin_rdpev_queue_ui (RemminaProtocolWidget *gp)
{
    RemminaPluginRdpData *gpdata;
    RemminaPluginRdpUiObject *ui;

    gpdata = GET_DATA (gp);
    LOCK_BUFFER (FALSE)
    gpdata->ui_handler = 0;
    UNLOCK_BUFFER (FALSE)
    
    while ((ui = (RemminaPluginRdpUiObject *) g_async_queue_try_pop (gpdata->ui_queue)) != NULL)
    {
        switch (ui->type)
        {
        case REMMINA_PLUGIN_RDP_UI_CONNECTED:
            remmina_plugin_rdpev_connected (gp, ui);
            break;
        case REMMINA_PLUGIN_RDP_UI_RECT:
            remmina_plugin_rdpev_rect (gp, ui);
            break;
        case REMMINA_PLUGIN_RDP_UI_CREATE_SURFACE:
            remmina_plugin_rdpev_create_surface (gp, ui);
            break;
        case REMMINA_PLUGIN_RDP_UI_SET_SURFACE:
            remmina_plugin_rdpev_set_surface (gp, ui);
            break;
        case REMMINA_PLUGIN_RDP_UI_DESTROY_SURFACE:
            remmina_plugin_rdpev_destroy_surface (gp, ui);
            break;
        case REMMINA_PLUGIN_RDP_UI_CREATE_BITMAP:
            remmina_plugin_rdpev_create_bitmap (gp, ui);
            break;
        case REMMINA_PLUGIN_RDP_UI_PAINT_BITMAP:
            remmina_plugin_rdpev_paint_bitmap (gp, ui);
            break;
        case REMMINA_PLUGIN_RDP_UI_DESTROY_BITMAP:
            remmina_plugin_rdpev_destroy_bitmap (gp, ui);
            break;
        default:
            break;
        }
        remmina_plugin_rdpui_object_free (ui);
    }

    return FALSE;
}

void
remmina_plugin_rdpev_unfocus (RemminaProtocolWidget *gp)
{
    remmina_plugin_rdpev_release_key (gp, 0);
}

