/*
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

   xrdp: A Remote Desktop Protocol server.
   Copyright (C) Jay Sorg 2005-2010

   libxup main file

*/

#include "xup.h"

/******************************************************************************/
/* returns error */
int DEFAULT_CC
lib_recv(struct mod* mod, char* data, int len)
{
  int rcvd;

  if (mod->sck_closed)
  {
    return 1;
  }
  while (len > 0)
  {
    rcvd = g_tcp_recv(mod->sck, data, len, 0);
    if (rcvd == -1)
    {
      if (g_tcp_last_error_would_block(mod->sck))
      {
        if (mod->server_is_term(mod))
        {
          return 1;
        }
        g_tcp_can_recv(mod->sck, 10);
      }
      else
      {
        return 1;
      }
    }
    else if (rcvd == 0)
    {
      mod->sck_closed = 1;
      return 1;
    }
    else
    {
      data += rcvd;
      len -= rcvd;
    }
  }
  return 0;
}

/*****************************************************************************/
/* returns error */
int DEFAULT_CC
lib_send(struct mod* mod, char* data, int len)
{
  int sent;

  if (mod->sck_closed)
  {
    return 1;
  }
  while (len > 0)
  {
    sent = g_tcp_send(mod->sck, data, len, 0);
    if (sent == -1)
    {
      if (g_tcp_last_error_would_block(mod->sck))
      {
        if (mod->server_is_term(mod))
        {
          return 1;
        }
        g_tcp_can_send(mod->sck, 10);
      }
      else
      {
        return 1;
      }
    }
    else if (sent == 0)
    {
      mod->sck_closed = 1;
      return 1;
    }
    else
    {
      data += sent;
      len -= sent;
    }
  }
  return 0;
}

/******************************************************************************/
/* return error */
int DEFAULT_CC
lib_mod_start(struct mod* mod, int w, int h, int bpp)
{
  LIB_DEBUG(mod, "in lib_mod_start");
  mod->width = w;
  mod->height = h;
  mod->bpp = bpp;
  LIB_DEBUG(mod, "out lib_mod_start");
  return 0;
}

/******************************************************************************/
/* return error */
int DEFAULT_CC
lib_mod_connect(struct mod* mod)
{
  int error;
  int len;
  int i;
  int index;
  int use_uds;
  struct stream* s;
  char con_port[256];

  LIB_DEBUG(mod, "in lib_mod_connect");
  /* clear screen */
  mod->server_begin_update(mod);
  mod->server_set_fgcolor(mod, 0);
  mod->server_fill_rect(mod, 0, 0, mod->width, mod->height);
  mod->server_end_update(mod);
  mod->server_msg(mod, "started connecting", 0);
  /* only support 8, 15, 16, and 24 bpp connections from rdp client */
  if (mod->bpp != 8 && mod->bpp != 15 && mod->bpp != 16 && mod->bpp != 24)
  {
    mod->server_msg(mod,
      "error - only supporting 8, 15, 16, and 24 bpp rdp connections", 0);
    LIB_DEBUG(mod, "out lib_mod_connect error");
    return 1;
  }
  if (g_strcmp(mod->ip, "") == 0)
  {
    mod->server_msg(mod, "error - no ip set", 0);
    LIB_DEBUG(mod, "out lib_mod_connect error");
    return 1;
  }
  make_stream(s);
  g_sprintf(con_port, "%s", mod->port);
  use_uds = 0;
  if (con_port[0] == '/')
  {
    use_uds = 1;
  }
  mod->sck_closed = 0;
  i = 0;
  while (1)
  {
    if (use_uds)
    {
      mod->sck = g_tcp_local_socket();
    }
    else
    {
      mod->sck = g_tcp_socket();
    }
    g_tcp_set_non_blocking(mod->sck);
    g_tcp_set_no_delay(mod->sck);
    mod->server_msg(mod, "connecting...", 0);
    if (use_uds)
    {
      error = g_tcp_local_connect(mod->sck, con_port);
    }
    else
    {
      error = g_tcp_connect(mod->sck, mod->ip, con_port);
    }
    if (error == -1)
    {
      if (g_tcp_last_error_would_block(mod->sck))
      {
        error = 0;
        index = 0;
        while (!g_tcp_can_send(mod->sck, 100))
        {
          index++;
          if ((index >= 30) || mod->server_is_term(mod))
          {
            mod->server_msg(mod, "connect timeout", 0);
            error = 1;
            break;
          }
        }
      }
      else
      {
        mod->server_msg(mod, "connect error", 0);
      }
    }
    if (error == 0)
    {
      break;
    }
    g_tcp_close(mod->sck);
    mod->sck = 0;
    i++;
    if (i >= 4)
    {
      mod->server_msg(mod, "connection problem, giving up", 0);
      break;
    }
    g_sleep(250);
  }
  if (error == 0)
  {
    /* send version message */
    init_stream(s, 8192);
    s_push_layer(s, iso_hdr, 4);
    out_uint16_le(s, 103);
    out_uint32_le(s, 301);
    out_uint32_le(s, 0);
    out_uint32_le(s, 0);
    out_uint32_le(s, 0);
    out_uint32_le(s, 1);
    s_mark_end(s);
    len = (int)(s->end - s->data);
    s_pop_layer(s, iso_hdr);
    out_uint32_le(s, len);
    lib_send(mod, s->data, len);
  }
  if (error == 0)
  {
    /* send screen size message */
    init_stream(s, 8192);
    s_push_layer(s, iso_hdr, 4);
    out_uint16_le(s, 103);
    out_uint32_le(s, 300);
    out_uint32_le(s, mod->width);
    out_uint32_le(s, mod->height);
    out_uint32_le(s, mod->bpp);
    out_uint32_le(s, 0);
    s_mark_end(s);
    len = (int)(s->end - s->data);
    s_pop_layer(s, iso_hdr);
    out_uint32_le(s, len);
    lib_send(mod, s->data, len);
  }
  if (error == 0)
  {
    /* send invalidate message */
    init_stream(s, 8192);
    s_push_layer(s, iso_hdr, 4);
    out_uint16_le(s, 103);
    out_uint32_le(s, 200);
    /* x and y */
    i = 0;
    out_uint32_le(s, i);
    /* width and height */
    i = ((mod->width & 0xffff) << 16) | mod->height;
    out_uint32_le(s, i);
    out_uint32_le(s, 0);
    out_uint32_le(s, 0);
    s_mark_end(s);
    len = (int)(s->end - s->data);
    s_pop_layer(s, iso_hdr);
    out_uint32_le(s, len);
    lib_send(mod, s->data, len);
  }
  free_stream(s);
  if (error != 0)
  {
    mod->server_msg(mod, "some problem", 0);
    LIB_DEBUG(mod, "out lib_mod_connect error");
    return 1;
  }
  else
  {
    mod->server_msg(mod, "connected ok", 0);
    mod->sck_obj = g_create_wait_obj_from_socket(mod->sck, 0);
  }
  LIB_DEBUG(mod, "out lib_mod_connect");
  return 0;
}

/******************************************************************************/
/* return error */
int DEFAULT_CC
lib_mod_event(struct mod* mod, int msg, tbus param1, tbus param2,
              tbus param3, tbus param4)
{
  struct stream* s;
  int len;
  int key;
  int rv;

  LIB_DEBUG(mod, "in lib_mod_event");
  make_stream(s);
  if ((msg >= 15) && (msg <= 16)) /* key events */
  {
    key = param2;
    if (key > 0)
    {
      if (key == 65027) /* altgr */
      {
        if (mod->shift_state)
        {
          g_writeln("special");
          /* fix for mstsc sending left control down with altgr */
          /* control down / up
          msg param1 param2 param3 param4
          15  0      65507  29     0
          16  0      65507  29     49152 */
          init_stream(s, 8192);
          s_push_layer(s, iso_hdr, 4);
          out_uint16_le(s, 103);
          out_uint32_le(s, 16); /* key up */
          out_uint32_le(s, 0);
          out_uint32_le(s, 65507); /* left control */
          out_uint32_le(s, 29); /* RDP scan code */
          out_uint32_le(s, 0xc000); /* flags */
          s_mark_end(s);
          len = (int)(s->end - s->data);
          s_pop_layer(s, iso_hdr);
          out_uint32_le(s, len);
          lib_send(mod, s->data, len);
        }
      }
      if (key == 65507) /* left control */
      {
        mod->shift_state = msg == 15;
      }
    }
  }
  init_stream(s, 8192);
  s_push_layer(s, iso_hdr, 4);
  out_uint16_le(s, 103);
  out_uint32_le(s, msg);
  out_uint32_le(s, param1);
  out_uint32_le(s, param2);
  out_uint32_le(s, param3);
  out_uint32_le(s, param4);
  s_mark_end(s);
  len = (int)(s->end - s->data);
  s_pop_layer(s, iso_hdr);
  out_uint32_le(s, len);
  rv = lib_send(mod, s->data, len);
  free_stream(s);
  LIB_DEBUG(mod, "out lib_mod_event");
  return rv;
}

/******************************************************************************/
/* return error */
int DEFAULT_CC
lib_mod_signal(struct mod* mod)
{
  struct stream* s;
  int num_orders;
  int index;
  int rv;
  int len;
  int type;
  int x;
  int y;
  int cx;
  int cy;
  int fgcolor;
  int opcode;
  int width;
  int height;
  int srcx;
  int srcy;
  int len_bmpdata;
  int style;
  int x1;
  int y1;
  int x2;
  int y2;
  char* phold;
  char* bmpdata;
  char cur_data[32 * (32 * 3)];
  char cur_mask[32 * (32 / 8)];

  LIB_DEBUG(mod, "in lib_mod_signal");
  make_stream(s);
  init_stream(s, 8192);
  rv = lib_recv(mod, s->data, 8);
  if (rv == 0)
  {
    in_uint16_le(s, type);
    in_uint16_le(s, num_orders);
    in_uint32_le(s, len);
    if (type == 1)
    {
      init_stream(s, len);
      rv = lib_recv(mod, s->data, len);
      if (rv == 0)
      {
        for (index = 0; index < num_orders; index++)
        {
          in_uint16_le(s, type);
          switch (type)
          {
            case 1: /* server_begin_update */
              rv = mod->server_begin_update(mod);
              break;
            case 2: /* server_end_update */
              rv = mod->server_end_update(mod);
              break;
            case 3: /* server_fill_rect */
              in_sint16_le(s, x);
              in_sint16_le(s, y);
              in_uint16_le(s, cx);
              in_uint16_le(s, cy);
              rv = mod->server_fill_rect(mod, x, y, cx, cy);
              break;
            case 4: /* server_screen_blt */
              in_sint16_le(s, x);
              in_sint16_le(s, y);
              in_uint16_le(s, cx);
              in_uint16_le(s, cy);
              in_sint16_le(s, srcx);
              in_sint16_le(s, srcy);
              rv = mod->server_screen_blt(mod, x, y, cx, cy, srcx, srcy);
              break;
            case 5: /* server_paint_rect */
              in_sint16_le(s, x);
              in_sint16_le(s, y);
              in_uint16_le(s, cx);
              in_uint16_le(s, cy);
              in_uint32_le(s, len_bmpdata);
              in_uint8p(s, bmpdata, len_bmpdata);
              in_uint16_le(s, width);
              in_uint16_le(s, height);
              in_sint16_le(s, srcx);
              in_sint16_le(s, srcy);
              rv = mod->server_paint_rect(mod, x, y, cx, cy,
                                          bmpdata, width, height,
                                          srcx, srcy);
              break;
            case 10: /* server_set_clip */
              in_sint16_le(s, x);
              in_sint16_le(s, y);
              in_uint16_le(s, cx);
              in_uint16_le(s, cy);
              rv = mod->server_set_clip(mod, x, y, cx, cy);
              break;
            case 11: /* server_reset_clip */
              rv = mod->server_reset_clip(mod);
              break;
            case 12: /* server_set_fgcolor */
              in_uint32_le(s, fgcolor);
              rv = mod->server_set_fgcolor(mod, fgcolor);
              break;
            case 14:
              in_uint16_le(s, opcode);
              rv = mod->server_set_opcode(mod, opcode);
              break;
            case 17:
              in_uint16_le(s, style);
              in_uint16_le(s, width);
              rv = mod->server_set_pen(mod, style, width);
              break;
            case 18:
              in_sint16_le(s, x1);
              in_sint16_le(s, y1);
              in_sint16_le(s, x2);
              in_sint16_le(s, y2);
              rv = mod->server_draw_line(mod, x1, y1, x2, y2);
              break;
            case 19:
              in_sint16_le(s, x);
              in_sint16_le(s, y);
              in_uint8a(s, cur_data, 32 * (32 * 3));
              in_uint8a(s, cur_mask, 32 * (32 / 8));
              rv = mod->server_set_cursor(mod, x, y, cur_data, cur_mask);
              break;
            default:
              rv = 1;
              break;
          }
          if (rv != 0)
          {
            break;
          }
        }
      }
    }
    else if (type == 2) /* caps */
    {
      g_writeln("lib_mod_signal: type 2 len %d\n", len);
      init_stream(s, len);
      rv = lib_recv(mod, s->data, len);
      if (rv == 0)
      {
        for (index = 0; index < num_orders; index++)
        {
          phold = s->p;
          in_uint16_le(s, type);
          in_uint16_le(s, len);
          switch (type)
          {
            default:
              g_writeln("lib_mod_signal: unknown cap type %d len %d", type, len);
              break;
          }
          s->p = phold + len;
        }
      }
    }
    else
    {
      g_writeln("unknown type %d", type);
    }
  }
  free_stream(s);
  LIB_DEBUG(mod, "out lib_mod_signal");
  return rv;
}

/******************************************************************************/
/* return error */
int DEFAULT_CC
lib_mod_end(struct mod* mod)
{
  return 0;
}

/******************************************************************************/
/* return error */
int DEFAULT_CC
lib_mod_set_param(struct mod* mod, char* name, char* value)
{
  if (g_strcasecmp(name, "username") == 0)
  {
    g_strncpy(mod->username, value, 255);
  }
  else if (g_strcasecmp(name, "password") == 0)
  {
    g_strncpy(mod->password, value, 255);
  }
  else if (g_strcasecmp(name, "ip") == 0)
  {
    g_strncpy(mod->ip, value, 255);
  }
  else if (g_strcasecmp(name, "port") == 0)
  {
    g_strncpy(mod->port, value, 255);
  }
  return 0;
}

/******************************************************************************/
/* return error */
int DEFAULT_CC
lib_mod_get_wait_objs(struct mod* mod, tbus* read_objs, int* rcount,
                      tbus* write_objs, int* wcount, int* timeout)
{
  int i;

  i = *rcount;
  if (mod != 0)
  {
    if (mod->sck_obj != 0)
    {
      read_objs[i++] = mod->sck_obj;
    }
  }
  *rcount = i;
  return 0;
}

/******************************************************************************/
/* return error */
int DEFAULT_CC
lib_mod_check_wait_objs(struct mod* mod)
{
  int rv;

  rv = 0;
  if (mod != 0)
  {
    if (mod->sck_obj != 0)
    {
      if (g_is_wait_obj_set(mod->sck_obj))
      {
        rv = lib_mod_signal(mod);
      }
    }
  }
  return rv;
}

/******************************************************************************/
struct mod* EXPORT_CC
mod_init(void)
{
  struct mod* mod;

  mod = (struct mod*)g_malloc(sizeof(struct mod), 1);
  mod->size = sizeof(struct mod);
  mod->version = CURRENT_MOD_VER;
  mod->handle = (tbus)mod;
  mod->mod_connect = lib_mod_connect;
  mod->mod_start = lib_mod_start;
  mod->mod_event = lib_mod_event;
  mod->mod_signal = lib_mod_signal;
  mod->mod_end = lib_mod_end;
  mod->mod_set_param = lib_mod_set_param;
  mod->mod_get_wait_objs = lib_mod_get_wait_objs;
  mod->mod_check_wait_objs = lib_mod_check_wait_objs;
  return mod;
}

/******************************************************************************/
int EXPORT_CC
mod_exit(struct mod* mod)
{
  if (mod == 0)
  {
    return 0;
  }
  g_delete_wait_obj_from_socket(mod->sck_obj);
  g_tcp_close(mod->sck);
  g_free(mod);
  return 0;
}
