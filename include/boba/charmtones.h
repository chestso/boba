/* charmtones.h - CharmTone color palette for boba
 *
 * The 70-color CharmTone palette from Charmbracelet (charm.land).
 * Each color is available as a TuiColor via a static inline constructor.
 *
 * The 16 ANSI base colors map to canonical CharmTone names:
 *   0=Pepper  1=Coral  2=Guac  3=Mustard  4=Charple  5=Dolly
 *   6=Turtle  7=Smoke  8=Oyster  9=Salmon  10=Julep  11=Zest
 *   12=Hazy  13=Blush  14=Bok  15=Butter
 *
 * Source: github.com/charmbracelet/x/exp/charmtone
 */

#ifndef BOBA_CHARMTONES_H
#define BOBA_CHARMTONES_H

#include "style.h"

/* ----- ANSI base 16 ----- */

static inline TuiColor tui_ct_pepper(void)
{
    return tui_color_rgb(0x20, 0x1f, 0x26);
}
static inline TuiColor tui_ct_coral(void)
{
    return tui_color_rgb(0xff, 0x57, 0x7d);
}
static inline TuiColor tui_ct_guac(void)
{
    return tui_color_rgb(0x12, 0xc7, 0x8f);
}
static inline TuiColor tui_ct_mustard(void)
{
    return tui_color_rgb(0xf5, 0xef, 0x34);
}
static inline TuiColor tui_ct_charple(void)
{
    return tui_color_rgb(0x6b, 0x50, 0xff);
}
static inline TuiColor tui_ct_dolly(void)
{
    return tui_color_rgb(0xff, 0x60, 0xff);
}
static inline TuiColor tui_ct_turtle(void)
{
    return tui_color_rgb(0x0a, 0xdc, 0xd9);
}
static inline TuiColor tui_ct_smoke(void)
{
    return tui_color_rgb(0xbf, 0xbc, 0xc8);
}

static inline TuiColor tui_ct_oyster(void)
{
    return tui_color_rgb(0x60, 0x5f, 0x6b);
}
static inline TuiColor tui_ct_salmon(void)
{
    return tui_color_rgb(0xff, 0x7f, 0x90);
}
static inline TuiColor tui_ct_julep(void)
{
    return tui_color_rgb(0x00, 0xff, 0xb2);
}
static inline TuiColor tui_ct_zest(void)
{
    return tui_color_rgb(0xe8, 0xfe, 0x96);
}
static inline TuiColor tui_ct_hazy(void)
{
    return tui_color_rgb(0x8b, 0x75, 0xff);
}
static inline TuiColor tui_ct_blush(void)
{
    return tui_color_rgb(0xff, 0x84, 0xff);
}
static inline TuiColor tui_ct_bok(void)
{
    return tui_color_rgb(0x68, 0xff, 0xd6);
}
static inline TuiColor tui_ct_butter(void)
{
    return tui_color_rgb(0xff, 0xfa, 0xf1);
}

/* ----- Warm spectrum ----- */

static inline TuiColor tui_ct_cumin(void)
{
    return tui_color_rgb(0xbf, 0x97, 0x6f);
}
static inline TuiColor tui_ct_tang(void)
{
    return tui_color_rgb(0xff, 0x98, 0x5a);
}
static inline TuiColor tui_ct_yam(void)
{
    return tui_color_rgb(0xff, 0xb5, 0x87);
}
static inline TuiColor tui_ct_paprika(void)
{
    return tui_color_rgb(0xd3, 0x6c, 0x64);
}
static inline TuiColor tui_ct_bengal(void)
{
    return tui_color_rgb(0xff, 0x6e, 0x63);
}
static inline TuiColor tui_ct_uni(void)
{
    return tui_color_rgb(0xff, 0x93, 0x7d);
}
static inline TuiColor tui_ct_sriracha(void)
{
    return tui_color_rgb(0xeb, 0x42, 0x68);
}
static inline TuiColor tui_ct_chili(void)
{
    return tui_color_rgb(0xe2, 0x30, 0x80);
}
static inline TuiColor tui_ct_cherry(void)
{
    return tui_color_rgb(0xff, 0x38, 0x8b);
}
static inline TuiColor tui_ct_tuna(void)
{
    return tui_color_rgb(0xff, 0x6d, 0xaa);
}
static inline TuiColor tui_ct_macaron(void)
{
    return tui_color_rgb(0xe9, 0x40, 0xb0);
}
static inline TuiColor tui_ct_pony(void)
{
    return tui_color_rgb(0xff, 0x4f, 0xbf);
}
static inline TuiColor tui_ct_cheeky(void)
{
    return tui_color_rgb(0xff, 0x79, 0xd0);
}
static inline TuiColor tui_ct_flamingo(void)
{
    return tui_color_rgb(0xf9, 0x47, 0xe3);
}

/* ----- Purple spectrum ----- */

static inline TuiColor tui_ct_urchin(void)
{
    return tui_color_rgb(0xc3, 0x37, 0xe0);
}
static inline TuiColor tui_ct_mochi(void)
{
    return tui_color_rgb(0xeb, 0x5d, 0xff);
}
static inline TuiColor tui_ct_lilac(void)
{
    return tui_color_rgb(0xf3, 0x79, 0xff);
}
static inline TuiColor tui_ct_prince(void)
{
    return tui_color_rgb(0x9c, 0x35, 0xe1);
}
static inline TuiColor tui_ct_violet(void)
{
    return tui_color_rgb(0xc2, 0x59, 0xff);
}
static inline TuiColor tui_ct_mauve(void)
{
    return tui_color_rgb(0xd4, 0x6e, 0xff);
}
static inline TuiColor tui_ct_grape(void)
{
    return tui_color_rgb(0x71, 0x34, 0xdd);
}
static inline TuiColor tui_ct_plum(void)
{
    return tui_color_rgb(0x99, 0x53, 0xff);
}
static inline TuiColor tui_ct_orchid(void)
{
    return tui_color_rgb(0xad, 0x6e, 0xff);
}
static inline TuiColor tui_ct_jelly(void)
{
    return tui_color_rgb(0x4a, 0x30, 0xd9);
}
static inline TuiColor tui_ct_darple(void)
{
    return tui_color_rgb(0x5b, 0x40, 0xec);
}
static inline TuiColor tui_ct_larple(void)
{
    return tui_color_rgb(0x7b, 0x62, 0xff);
}

/* ----- Blue spectrum ----- */

static inline TuiColor tui_ct_ox(void)
{
    return tui_color_rgb(0x33, 0x31, 0xb2);
}
static inline TuiColor tui_ct_sapphire(void)
{
    return tui_color_rgb(0x49, 0x49, 0xff);
}
static inline TuiColor tui_ct_guppy(void)
{
    return tui_color_rgb(0x72, 0x72, 0xff);
}
static inline TuiColor tui_ct_oceania(void)
{
    return tui_color_rgb(0x2b, 0x55, 0xb3);
}
static inline TuiColor tui_ct_thunder(void)
{
    return tui_color_rgb(0x47, 0x76, 0xff);
}
static inline TuiColor tui_ct_anchovy(void)
{
    return tui_color_rgb(0x71, 0x9a, 0xfc);
}
static inline TuiColor tui_ct_damson(void)
{
    return tui_color_rgb(0x00, 0x7a, 0xb8);
}
static inline TuiColor tui_ct_malibu(void)
{
    return tui_color_rgb(0x00, 0xa4, 0xff);
}
static inline TuiColor tui_ct_sardine(void)
{
    return tui_color_rgb(0x4f, 0xbe, 0xfe);
}

/* ----- Cyan/green spectrum ----- */

static inline TuiColor tui_ct_zinc(void)
{
    return tui_color_rgb(0x10, 0xb1, 0xae);
}
static inline TuiColor tui_ct_lichen(void)
{
    return tui_color_rgb(0x5c, 0xdf, 0xea);
}
static inline TuiColor tui_ct_pickle(void)
{
    return tui_color_rgb(0x00, 0xa4, 0x75);
}
static inline TuiColor tui_ct_gator(void)
{
    return tui_color_rgb(0x18, 0x46, 0x3d);
}
static inline TuiColor tui_ct_spinach(void)
{
    return tui_color_rgb(0x1c, 0x36, 0x34);
}

/* ----- Yellow spectrum ----- */

static inline TuiColor tui_ct_citron(void)
{
    return tui_color_rgb(0xe8, 0xff, 0x27);
}

/* ----- Neutrals ----- */

static inline TuiColor tui_ct_bbq(void)
{
    return tui_color_rgb(0x2d, 0x2c, 0x36);
}
static inline TuiColor tui_ct_char(void)
{
    return tui_color_rgb(0x3a, 0x39, 0x43);
}
static inline TuiColor tui_ct_iron(void)
{
    return tui_color_rgb(0x4d, 0x4c, 0x57);
}
static inline TuiColor tui_ct_squid(void)
{
    return tui_color_rgb(0x85, 0x83, 0x92);
}
static inline TuiColor tui_ct_steam(void)
{
    return tui_color_rgb(0xa2, 0xa0, 0xad);
}
static inline TuiColor tui_ct_steep(void)
{
    return tui_color_rgb(0xd6, 0xd3, 0xdc);
}
static inline TuiColor tui_ct_sash(void)
{
    return tui_color_rgb(0xec, 0xeb, 0xf0);
}
static inline TuiColor tui_ct_salt(void)
{
    return tui_color_rgb(0xf7, 0xf6, 0xfb);
}
static inline TuiColor tui_ct_soda(void)
{
    return tui_color_rgb(0xfb, 0xfb, 0xfb);
}

/* ----- Dark variants ----- */

static inline TuiColor tui_ct_pom(void)
{
    return tui_color_rgb(0xab, 0x24, 0x54);
}
static inline TuiColor tui_ct_steak(void)
{
    return tui_color_rgb(0x58, 0x22, 0x38);
}
static inline TuiColor tui_ct_toast(void)
{
    return tui_color_rgb(0x41, 0x21, 0x30);
}

/* ----- Special ----- */

static inline TuiColor tui_ct_ice(void)
{
    return tui_color_rgb(0x00, 0xff, 0xfc);
}

#endif /* BOBA_CHARMTONES_H */
