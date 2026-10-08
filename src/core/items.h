#pragma once

/* ------------------------------------------------------------------ */
/* Item population                                                     */
/* ------------------------------------------------------------------ */

static Item *add_item(int m, int kind, const char *title, const char *sub, const char *id,
                      vita2d_texture *stock)
{
	Menu *mn = &menus[m];
	if (mn->count >= mn->cap) return NULL;
	Item *it = &mn->items[mn->count++];
	memset(it, 0, sizeof(*it));
	it->kind = kind;
	it->submenu = -1;
	it->stock = stock;
	snprintf(it->title, sizeof(it->title), "%s", title);
	snprintf(it->sub, sizeof(it->sub), "%s", sub ? sub : "");
	snprintf(it->id, sizeof(it->id), "%s", id ? id : "");
	return it;
}
