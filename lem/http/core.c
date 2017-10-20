/*
 * This file is part of LEM, a Lua Event Machine.
 * Copyright 2011-2013 Emil Renner Berthing
 *
 * LEM is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * LEM is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with LEM.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <lem-parsers.h>
#include <ctype.h>

static int
_lem_urldecode(const char *src, size_t len, char *out) {
	size_t i;
	char *out_start = out;
	char a, b;
	for(i=0;i<len;i++) {
		if ((src[i] == '%')
			&& (i+2 < len)
			&& (a = src[i+1])
			&& (b = src[i+2])) {
			if ((!isxdigit(a)) ||
					(!isxdigit(b)))
				return -1;

			if (a >= 'a')
				a -= 'a'-'A';
			else if (a >= 'A')
				a -= ('A' - 10);
			else
				a -= '0';

			if (b >= 'a')
				b -= 'a'-'A';
			else if (b >= 'A')
				b -= ('A' - 10);
			else
				b -= '0';
			*out++ = 16*a+b;
			i+=2;
		} else if (src[i] == '+') {
			*out++ = ' ';
		} else {
			*out++ = src[i];
		}
	}
	return out - out_start;
}


enum classes {
	C_CTL,   /* control characters */
	C_LF,    /* \n */
	C_CR,    /* \r */
	C_LWS,   /* space or \t */
	C_TSPCL, /* tspecials */
	C_SLASH, /* / */
	C_COLON, /* : */
	C_DOT,   /* . */
	C_NUM,   /* 0-9 */
	C_H,     /* H */
	C_T,     /* T */
	C_P,     /* P */
	C_ETC,   /* the rest */
	C_MAX
};

/*
 * This array maps the first 128 ASCII characters into character classes
 * The remaining characters should be mapped to C_ETC
 */
static const unsigned char ascii_class[128] = {
	C_CTL,   C_CTL,   C_CTL,   C_CTL,   C_CTL,   C_CTL,   C_CTL,   C_CTL,
	C_CTL,   C_LWS,   C_LF,    C_CTL,   C_CTL,   C_CR,    C_CTL,   C_CTL,
	C_CTL,   C_CTL,   C_CTL,   C_CTL,   C_CTL,   C_CTL,   C_CTL,   C_CTL,
	C_CTL,   C_CTL,   C_CTL,   C_CTL,   C_CTL,   C_CTL,   C_CTL,   C_CTL,

	C_LWS,   C_ETC,   C_TSPCL, C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,
	C_TSPCL, C_TSPCL, C_ETC,   C_ETC,   C_TSPCL, C_ETC,   C_DOT,   C_SLASH,
	C_NUM,   C_NUM,   C_NUM,   C_NUM,   C_NUM,   C_NUM,   C_NUM,   C_NUM,
	C_NUM,   C_NUM,   C_COLON, C_TSPCL, C_TSPCL, C_TSPCL, C_TSPCL, C_TSPCL,

	C_TSPCL, C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,
	C_H,     C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,
	C_P,     C_ETC,   C_ETC,   C_ETC,   C_T,     C_ETC,   C_ETC,   C_ETC,
	C_ETC,   C_ETC,   C_ETC,   C_TSPCL, C_TSPCL, C_TSPCL, C_ETC,   C_ETC,

	C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,
	C_H,     C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,
	C_P,     C_ETC,   C_ETC,   C_ETC,   C_T,     C_ETC,   C_ETC,   C_ETC,
	C_ETC,   C_ETC,   C_ETC,   C_TSPCL, C_ETC,   C_TSPCL, C_ETC,   C_CTL
};

enum states {
	S_GO,
	SMTD,
	SMUS,
	SURI,
	SUHS,
	SH__,
	SHT1,
	SHT2,
	SHTP,
	SSLH,
	SMAV,
	SDOT,
	SMIV,
	C_GO,
	CH__,
	CHT1,
	CHT2,
	CHTP,
	CSLH,
	CMAV,
	CDOT,
	CMIV,
	CVNS,
	CNUM,
	CNTS,
	CTXT,
	SRE1,
	SRE2,
	SKEY,
	SCOL,
	SVAL,
	SLWS,
	SCN1,
	SCN2,
	SNL1,
	SNL2,
	SEND,
	SMAX,
	X___ = SMAX,
	XMUS,
	XUHS,
	XVNS,
	XNTS,
	XRE1,
	XKEY,
	XCOL,
	XVAL,
	XEND
};

static const unsigned char state_table[SMAX][C_MAX] = {
/*           ctl  \n   \r   lws  tsp  /    :    .   num   H    T    P    etc */
/* S_GO*/ { X___,X___,X___,X___,X___,X___,X___,SMTD,SMTD,SMTD,SMTD,SMTD,SMTD },
/* SMTD*/ { X___,X___,X___,XMUS,X___,X___,X___,SMTD,SMTD,SMTD,SMTD,SMTD,SMTD },
/* SMUS*/ { X___,X___,X___,SMUS,SURI,SURI,SURI,SURI,SURI,SURI,SURI,SURI,SURI },
/* SURI*/ { X___,X___,X___,XUHS,SURI,SURI,SURI,SURI,SURI,SURI,SURI,SURI,SURI },
/* SUHS*/ { X___,X___,X___,SUHS,X___,X___,X___,X___,X___,SH__,X___,X___,X___ },
/* SH__*/ { X___,X___,X___,X___,X___,X___,X___,X___,X___,X___,SHT1,X___,X___ },
/* SHT1*/ { X___,X___,X___,X___,X___,X___,X___,X___,X___,X___,SHT2,X___,X___ },
/* SHT2*/ { X___,X___,X___,X___,X___,X___,X___,X___,X___,X___,X___,SHTP,X___ },
/* SHTP*/ { X___,X___,X___,SHTP,X___,SSLH,X___,X___,X___,X___,X___,X___,X___ },
/* SSLH*/ { X___,X___,X___,SSLH,X___,X___,X___,X___,SMAV,X___,X___,X___,X___ },
/* SMAV*/ { X___,X___,X___,X___,X___,X___,X___,SDOT,SMAV,X___,X___,X___,X___ },
/* SDOT*/ { X___,X___,X___,X___,X___,X___,X___,X___,SMIV,X___,X___,X___,X___ },
/* SMIV*/ { X___,X___,SRE1,X___,X___,X___,X___,X___,SMIV,X___,X___,X___,X___ },
/* C_GO*/ { X___,X___,X___,X___,X___,X___,X___,X___,X___,CH__,X___,X___,X___ },
/* CH__*/ { X___,X___,X___,X___,X___,X___,X___,X___,X___,X___,CHT1,X___,X___ },
/* CHT1*/ { X___,X___,X___,X___,X___,X___,X___,X___,X___,X___,CHT2,X___,X___ },
/* CHT2*/ { X___,X___,X___,X___,X___,X___,X___,X___,X___,X___,X___,CHTP,X___ },
/* CHTP*/ { X___,X___,X___,CHTP,X___,CSLH,X___,X___,X___,X___,X___,X___,X___ },
/* CSLH*/ { X___,X___,X___,CSLH,X___,X___,X___,X___,CMAV,X___,X___,X___,X___ },
/* CMAV*/ { X___,X___,X___,X___,X___,X___,X___,CDOT,CMAV,X___,X___,X___,X___ },
/* CDOT*/ { X___,X___,X___,X___,X___,X___,X___,X___,CMIV,X___,X___,X___,X___ },
/* CMIV*/ { X___,X___,X___,XVNS,X___,X___,X___,X___,CMIV,X___,X___,X___,X___ },
/* CVNS*/ { X___,X___,X___,CVNS,X___,X___,X___,X___,CNUM,X___,X___,X___,X___ },
/* CNUM*/ { X___,X___,X___,XNTS,X___,X___,X___,X___,CNUM,X___,X___,X___,X___ },
/* CNTS*/ { X___,X___,X___,CNTS,CTXT,X___,X___,X___,CTXT,CTXT,CTXT,CTXT,CTXT },
/* CTXT*/ { X___,X___,XRE1,CTXT,CTXT,X___,X___,X___,CTXT,CTXT,CTXT,CTXT,CTXT },
/* SRE1*/ { X___,SRE2,X___,X___,X___,X___,X___,X___,X___,X___,X___,X___,X___ },
/* SRE2*/ { X___,X___,SEND,X___,X___,X___,X___,SKEY,SKEY,SKEY,SKEY,SKEY,SKEY },
/* SKEY*/ { X___,X___,X___,X___,X___,X___,XCOL,SKEY,SKEY,SKEY,SKEY,SKEY,SKEY },
/* SCOL*/ { X___,X___,SCN1,SCOL,SVAL,SVAL,SVAL,SVAL,SVAL,SVAL,SVAL,SVAL,SVAL },
/* SVAL*/ { X___,X___,SNL1,SLWS,SVAL,SVAL,SVAL,SVAL,SVAL,SVAL,SVAL,SVAL,SVAL },
/* SLWS*/ { X___,X___,SNL1,SLWS,XVAL,XVAL,XVAL,XVAL,XVAL,XVAL,XVAL,XVAL,XVAL },
/* SCN1*/ { X___,SCN2,X___,X___,X___,X___,X___,X___,X___,X___,X___,X___,X___ },
/* SCN2*/ { X___,X___,SEND,SCOL,X___,X___,X___,XKEY,XKEY,XKEY,XKEY,XKEY,XKEY },
/* SNL1*/ { X___,SNL2,X___,X___,X___,X___,X___,X___,X___,X___,X___,X___,X___ },
/* SNL2*/ { X___,X___,SEND,SLWS,X___,X___,X___,XKEY,XKEY,XKEY,XKEY,XKEY,XKEY },
/* SEND*/ { X___,XEND,X___,X___,X___,X___,X___,X___,X___,X___,X___,X___,X___ },
};

struct parse_http_state {
	unsigned int w;
	unsigned char state;
	unsigned int header_line_off;
};
LEM_BUILD_ASSERT(sizeof(struct parse_http_state) < LEM_INPUTBUF_PSIZE);

static void
parse_http_init(lua_State *T)
{
	/* create result table */
	lua_settop(T, 2);
	lua_createtable(T, 0, 5);
}

static void
parse_http_req_init(lua_State *T, struct lem_inputbuf *b)
{
	struct parse_http_state *s = (struct parse_http_state *)&b->pstate;

	s->w = 0;
	s->state = S_GO;
	s->header_line_off = 1;

	parse_http_init(T);
}

static void
parse_http_res_init(lua_State *T, struct lem_inputbuf *b)
{
	struct parse_http_state *s = (struct parse_http_state *)&b->pstate;

	s->w = 0;
	s->state = C_GO;
	s->header_line_off = 1;

	parse_http_init(T);
}

static int
parse_http_process(lua_State *T, struct lem_inputbuf *b)
{
	struct parse_http_state *s = (struct parse_http_state *)&b->pstate;
	unsigned int header_line_off = s->header_line_off;
	unsigned int w = s->w;
	unsigned int r = b->start;
	unsigned char state = s->state;
	unsigned char ch;

	while (likely(r < b->end)) {
		ch = b->buf[r++];

		if (likely(ch < 127)) {
			state = state_table[state][ascii_class[ch]];
		} else {
			state = state_table[state][C_ETC];
		}

		//state = state_table[state][ch > 127 ? C_ETC : ascii_class[ch]];
		/*lem_debug("char = %c (%hhu), state = %hhu", ch, ch, state);*/
		switch (state) {
		case SMTD:
		case SURI:
		case SMAV:
		case SDOT:
		case SMIV:
		case CMAV:
		case CDOT:
		case CMIV:
		case CNUM:
		case CTXT:
		case SVAL:
			b->buf[w++] = ch;
			break;

		case XKEY:
			state = SKEY;
			lua_pushlstring(T, b->buf, w);
			lua_rawseti(T, -2, 2);
			lua_rawseti(T, -2, header_line_off++);
			w = 0;
			/* fallthrough */

		case SKEY:
			b->buf[w++] = ch;
			break;

		case SRE1:
			lua_pushlstring(T, b->buf, w);
			lua_setfield(T, -2, "version");
			w = 0;
			lua_newtable(T);
			break;

		case X___:
			lem_debug("HTTP parse error");
			lua_settop(T, 0);
			lua_pushnil(T);
			lua_pushliteral(T, "parse error");
			return 2;

		case XMUS:
			state = SMUS;
			lua_pushlstring(T, b->buf, w);
			lua_setfield(T, -2, "method");
			w = 0;
			break;

		case XUHS:
			state = SUHS;
			{
				size_t i;
				const char *tbuf = b->buf;
				for(i=0;i<w;i++) {
					if (tbuf[i] == '?') break;
				}

				char uri_buf[i];
				int uri_len = _lem_urldecode(b->buf, i, uri_buf);
				if (likely(uri_len != -1)) {
					lua_pushlstring(T, uri_buf, uri_len);
				} else {
					lua_pushnil(T);
				}
				lua_setfield(T, -2, "path");
			}

			lua_pushlstring(T, b->buf, w);
			lua_setfield(T, -2, "uri");
			w = 0;
			break;

		case XVNS:
			state = CVNS;
			lua_pushlstring(T, b->buf, w);
			lua_setfield(T, -2, "version");
			w = 0;
			break;

		case XNTS:
			state = CNTS;
			{
				unsigned int n = 0;
				unsigned int k;

				for (k = 0; k < w; k++) {
					n *= 10;
					n += b->buf[k] - '0';
				}

				lua_pushinteger(T, n);
			}
			lua_setfield(T, -2, "status");
			w = 0;
			break;

		case XRE1:
			state = SRE1;
			lua_pushlstring(T, b->buf, w);
			lua_setfield(T, -2, "text");
			w = 0;
			lua_newtable(T);
			break;

		case XCOL:
			state = SCOL;

			lua_createtable(T, 2, 0);
			lua_pushlstring(T, b->buf, w);
			lua_rawseti(T, -2, 1);
			w = 0;
			break;

		case XVAL:
			state = SVAL;
			b->buf[w++] = ' ';
			b->buf[w++] = ch;
			break;

		case XEND:
			if (w) {
				lua_pushlstring(T, b->buf, w);
			} else {
				lua_pushliteral(T, "");
			}
			lua_rawseti(T, -2, 2);
			lua_rawseti(T, -2, header_line_off++);

			lua_setfield(T, -2, "header_list");

			if (r == b->end)
				b->start = b->end = 0;
			else
				b->start = r;
			return 1;
		}
	}

	if (unlikely(w == LEM_INPUTBUF_SIZE - 1)) {
		b->start = b->end = 0;
		lua_settop(T, 0);
		lua_pushnil(T);
		lua_pushliteral(T, "out of buffer space");
		return 2;
	}

	b->start = b->end = w + 1;
	s->w = w;
	s->state = state;
	return 0;
}

static const struct lem_parser http_req_parser = {
	.init = parse_http_req_init,
	.process = parse_http_process,
};

static const struct lem_parser http_res_parser = {
	.init = parse_http_res_init,
	.process = parse_http_process,
};

static int
lem_urldecode(lua_State *T)
{
	size_t len;
	const char *str = lua_tolstring(T, 1, &len);
	char out[len+1];
	int out_len = _lem_urldecode(str, len, out);

	if (out_len == -1) {
		lua_pushnil(T);
		lua_pushstring(T, "encoding error");
		return 2;
	}

	out[out_len] = 0;
	lua_pushlstring(T, out, out_len);

	return 1;
}

int
luaopen_lem_http_core(lua_State *L)
{
	/* create module table M */
	lua_newtable(L);

	lua_pushlightuserdata(L, (void *)&http_req_parser);
	lua_setfield(L, -2, "HTTPRequest");
	lua_pushlightuserdata(L, (void *)&http_res_parser);
	lua_setfield(L, -2, "HTTPResponse");

	lua_pushcfunction(L, lem_urldecode);
	lua_setfield(L, -2, "urldecode");

	return 1;
}
