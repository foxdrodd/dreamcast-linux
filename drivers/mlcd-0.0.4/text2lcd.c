/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *
 *  This program converts a given string into a bitmapped font.
 *
 *  ChangeLog:
 *              2003-10-15  Christian Berger (c.berger@tu-braunschweig.de)
 *                          Code cleanup.
 *
 *              2003-10-13  Christian Berger (c.berger@tu-braunschweig.de)
 *                          Pointer problem fixed.
 */

#include <stdio.h>

#define VERSION "0.0.3"

/* Blank. */
static const char _[7] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

/* The bitmapped alphabet in upper case. */
static const char ALPHABET[26][7] = {
    { 0x00, 0x06, 0x09, 0x09, 0x0f, 0x09, 0x09 }, /*A*/
    { 0x00, 0x0e, 0x09, 0x0a, 0x0d, 0x09, 0x0e }, /*B*/
    { 0x00, 0x06, 0x09, 0x08, 0x08, 0x09, 0x06 }, /*C*/
    { 0x00, 0x0e, 0x09, 0x09, 0x09, 0x09, 0x0e }, /*D*/
    { 0x00, 0x0f, 0x08, 0x08, 0x0e, 0x08, 0x0f }, /*E*/
    { 0x00, 0x0f, 0x08, 0x08, 0x0e, 0x08, 0x08 }, /*F*/
    { 0x00, 0x06, 0x09, 0x08, 0x0b, 0x09, 0x06 }, /*G*/
    { 0x00, 0x09, 0x09, 0x09, 0x0f, 0x09, 0x09 }, /*H*/
    { 0x00, 0x0f, 0x04, 0x04, 0x04, 0x04, 0x0f }, /*I*/
    { 0x00, 0x0f, 0x01, 0x01, 0x01, 0x09, 0x06 }, /*J*/
    { 0x00, 0x09, 0x09, 0x0a, 0x0e, 0x0a, 0x09 }, /*K*/
    { 0x00, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0f }, /*L*/
    { 0x00, 0x05, 0x0a, 0x0b, 0x09, 0x09, 0x09 }, /*M*/
    { 0x00, 0x09, 0x0d, 0x0d, 0x0b, 0x0b, 0x09 }, /*N*/
    { 0x00, 0x06, 0x09, 0x09, 0x09, 0x09, 0x06 }, /*O*/
    { 0x00, 0x0e, 0x09, 0x09, 0x0e, 0x08, 0x08 }, /*P*/
    { 0x00, 0x06, 0x09, 0x09, 0x09, 0x0a, 0x05 }, /*Q*/
    { 0x00, 0x0e, 0x09, 0x09, 0x0e, 0x0a, 0x09 }, /*R*/
    { 0x00, 0x06, 0x09, 0x0c, 0x02, 0x09, 0x06 }, /*S*/
    { 0x00, 0x0f, 0x04, 0x04, 0x04, 0x04, 0x04 }, /*T*/
    { 0x00, 0x09, 0x09, 0x09, 0x09, 0x09, 0x06 }, /*U*/
    { 0x00, 0x09, 0x09, 0x09, 0x09, 0x06, 0x02 }, /*V*/
    { 0x00, 0x09, 0x09, 0x09, 0x0b, 0x0a, 0x05 }, /*W*/
    { 0x00, 0x09, 0x09, 0x06, 0x06, 0x09, 0x09 }, /*X*/
    { 0x00, 0x09, 0x09, 0x05, 0x02, 0x02, 0x02 }, /*Y*/
    { 0x00, 0x0f, 0x01, 0x02, 0x04, 0x08, 0x0f }  /*Z*/
};

int main(int argc, char** argv) {
    int i = 0, j = 0, old_i = 0;
    int rel_char_pos = -1, abs_char_pos = 0; /* Positions in the bitmapped font and in the original string. */
    int mask = 0, remainder = 0; /* Bitmask for hiding unnecessary bits from a bitmapped character. */
    int character = 0; /* Value from the corresponding bitmapped font for one character. */
    int byte = 0; /* The calculated value of one byte of the final bitmap. */
    int stop_pos = -1; /* Index of the last character in the string argv[1]. */

    enum bool {
        false=0,
        true
    } stopper = false;

    while (j < 32) {
        while (i < 48) {
            /* Compute the needed index for the input string. */
            if ( ((i%5) == 0) && (i < 45) ) {
                /* Changed due to porting issues. */
                /*rel_char_pos = (++rel_char_pos%9);*/

                rel_char_pos = rel_char_pos + 1;
                /*             /--------------\ --> 9 characters fit into one single line on the display. */
                rel_char_pos = rel_char_pos % 9;
            }

            /* Absolute index for the input string. */
            /*             /----------\ --> Relative position in one single line. */
            /*                            /-------\ --> Which line is printed on the display ? */
            abs_char_pos = rel_char_pos + ((j/7)*9);

            /* Stop on binary zero. */
            if ( (!stopper) && (argc == 2) && (argv[1][abs_char_pos] == '\0') ) {
                stopper = true;
                stop_pos = abs_char_pos;
            }

            /* Compute the needed bitmap value from the alphabetical matrix above. */
            if ( (argc == 2) && ( (stop_pos == -1) || (abs_char_pos < stop_pos) ) && (i<45) )
            {
                /* FIXME: Zeichenmenge eingrenzen!*/

                /* Only allow [A-Z\ ]. */
                if (argv[1][abs_char_pos] == 32)
                    character = _[(j%7)];
                else {
                    if ( (argv[1][abs_char_pos] >= 65) && (argv[1][abs_char_pos] <= 90) ) {
                        character = ALPHABET[argv[1][abs_char_pos] - 65][(j%7)];
                    }
                }
            }
            else {
                /* If we read the binary zero, we pad with blanks. */
                character = _[(j%7)];
            }

            /* Compute the needed bitmask. */
            /*        /--------------\ --> Can we insert a 5 bits wide character font? */
            /*                           /--\ --> Set mask for 5 bits (= 31 = 0x1f).*/
            /*                                               /---------\ --> Available space. */
            /*                                                v---> Characters are 5 bits wide (43210). */
            /*                                        /----------------\ --> Left shift of the 5 bit mask minding the available space. */
            /*                                   /---\ --> Hide the unnecessary bits. */
            /* mask = ( (8-(i%8)) > 4) ? 0x1f : (0x1f&(0x1f<<(5-(8-(i%8))))); */

            /*                               /------------------------\ --> Simple transformation. */
            mask = ( (8-(i%8)) > 4) ? 0x1f : (0x1f&(0x1f<<( (i%8) - 3)));

            /* Compute the remainder mask for the rest of the bitmapped font. */
            remainder = 0x1f - mask;

            /* Amount of the remaining bits. */
            /* old_i = 5 - (8-(i%8)); */
            /* Simple transformation. */
            old_i = (i%8) - 3;

            /* Process only the first 45 bits of one single line. */
            if (i<=40) {
                /* Right shift (out of the target byte!!!):
                   First, orient each bit on the left margin and then shift the bits right to their correct position. */
                byte += ((mask & character)*8)>>(i%8);
            }

            /*  /--------------\ --> We could completely insert one character. */
            /*                     /-------\ --> The character could only be inserted partly. */
            i+= (!remainder) ? 5 : (8-(i%8));  /* Increment the index. */

            /* Print one byte. */
            if (!(i%8)) {
                /* Character format for using with the display driver: */
                fprintf(stdout, "%c", byte);

                /* Hexadecimal format for console: */
                fprintf(stderr, "0x%x, ", byte);

                /* Reset the byte. */
                byte = 0;
            }

            /* In other cases feed with the remaining bits. */
            if (remainder != 0) {
                /* Process only the first 45 bits of one single line. */
                if (i<=40) {
                    /*                              /-------\ Left shift for the bits needed to be processed! */
                    byte+= (remainder & character)<<(8-old_i);
                }
                i+=old_i; /* Increment the index. */
            }
        }
        /* Start at the beginnig of the next line. */
        i=0;
        /* Process the next line. */
        j++;
    }

    /* Do a line feed on the console: */
    fprintf(stderr, "\n");

    return 0;
}
