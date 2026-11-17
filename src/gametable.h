/*
 * neo-pi - buttons A..D by the .neo header's NGH number
 */
#ifndef NEOPI_GAMETABLE_H
#define NEOPI_GAMETABLE_H

#define NEO_BUTTONS	4

// the table's index for the NGH number, -1 if the game is not in it
int NeoButtonsFind (unsigned nNGH);

// the names of buttons A..D for an index from NeoButtonsFind: 0 after the
// last, "" for a button the game does not use
const char *const *NeoButtons (int nIndex);

#endif
