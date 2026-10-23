#pragma once
#include <string>
#include <vector>
#include <gctypes.h>

/* Profile pictures.  A profile's picture is one of:
 *   ""             the first letter of its name (as before)
 *   "mii:XXXXXXXX" a Mii of this Wii (its Mii ID), read from the Mii
 *                  Channel's database on the NAND and drawn as a portrait
 *                  from its features (its face, skin, hair, eyes, mouth,
 *                  glasses, beard, favourite colour behind)
 *   "file:NAME"    a PNG or JPEG in sd:/apps/WiiFin/pictures/
 * Ui::avatar draws it, or the letter when it can't be found. */
namespace Avatars {

struct Mii {
    std::string id;          /* "mii:XXXXXXXX" */
    std::string name;        /* UTF-8 */
    u8 raw[74];
};

/* WiiFin's folder (with its '/'): pictures/ in it */
void setDir(const std::string& appDir);

/* the Miis of this Wii, read once (none: the database unreadable) */
const std::vector<Mii>& miis();
/* the pictures in pictures/ ("file:NAME"), read again each time */
std::vector<std::string> files();

/* the profile open (the header's avatar) */
void setCurrent(const std::string& pic);
const std::string& current();

/* a short name for a picture: the Mii's name, the file's */
std::string label(const std::string& pic);

/* pic drawn in a disc of radius r; false: nothing to draw (the letter then) */
bool draw(const std::string& pic, float cx, float cy, float r);

} // namespace Avatars
