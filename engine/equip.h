// SPDX-License-Identifier: GPL-2.0-or-later
/* equip.h — readied-equipment slot model (docs/rules/equipment.md).
 *
 * Eight body slots per actor; an item's CLASS (its preferred slot, from the
 * readiable table §3) plus the placement rules (§2) decide where it lands:
 * one-handed ARM items overflow to the off hand, TWO-HANDED weapons take both
 * arm slots, rings get two HAND slots, everything else is one fixed slot.
 * The combat fold (§4) collapses a readied set to the single weapon + summed
 * armour class the combat engine (combat.md CB3b) consumes.
 */
#ifndef U6E_EQUIP_H
#define U6E_EQUIP_H

#include <stdint.h>

enum { EQ_HEAD=0, EQ_NECK, EQ_BODY, EQ_ARM, EQ_ARM2, EQ_HAND, EQ_HAND2, EQ_FOOT, EQ_NSLOTS };

/* Readiable class of obj_n: the PREFERRED slot (EQ_*), where EQ_ARM2 means
 * "two-handed" (needs both arms). -1 = not readiable. */
int equip_class(int obj_n);

/* Ready obj_n into set[EQ_NSLOTS] (0 = empty slot). Returns the slot used
 * (>= 0), or EQUIP_ENOTREADIABLE / EQUIP_EFULL. */
enum { EQUIP_ENOTREADIABLE = -1, EQUIP_EFULL = -2 };
int equip_ready(uint16_t set[EQ_NSLOTS], int obj_n);

/* Remove the item in `slot`; returns its obj_n (0 if the slot was empty). */
int equip_unready(uint16_t set[EQ_NSLOTS], int slot);

/* §4 combat fold: weapon = readied item with the largest weapon_dmg (0 = unarmed),
 * ac = sum of armour_ac over the set. */
void equip_fold(const uint16_t set[EQ_NSLOTS], uint16_t *weapon, uint8_t *ac);

#endif /* U6E_EQUIP_H */
