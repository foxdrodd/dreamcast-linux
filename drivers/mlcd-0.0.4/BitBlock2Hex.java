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
 *  This program reads rectangles sized 5x7 from a given file containing zeros and ones
 *  for producing char arrays used by the tool text2lcd.c
 *
 *  ChangeLog:
 *			  2003-10-14  Christian Berger (c.berger@tu-braunschweig.de)
 *						  Code cleanup.
 *
 *			  2003-05-03  Christian Berger (c.berger@tu-braunschweig.de)
 *						  Start of work.
 */

import java.io.BufferedReader;
import java.io.FileReader;
import java.io.IOException;

/**
 * This program reads rectangles sized 5x7 from a given file containing zeros and ones
 * for producing char arrays used by the tool text2lcd.c
 *
 * For example:
 * static const char A[7] = { 0x00, 0x0c, 0x12, 0x12, 0x1e, 0x12, 0x12 };
 *
 * @author Christian Berger (c.berger@tu-braunschweig.de)
 * @version 0.01
 */
public class BitBlock2Hex
{
	public static void main(String argv[])
	{
		if (argv.length != 1)
		{
			System.err.println("Please start with \"java BitBlock2Hex filename\"!");
			System.exit(1);
		}

		System.err.println("Processing argv[0] = " + argv[0]);
		try
		{
			BufferedReader br = new BufferedReader(new FileReader(argv[0]));

			int value = 0, count = 0;
			String line = "", output = "";

			while ((line = br.readLine()) != null)
			{
				// Initialize variables.
				if (line.length() < 3)
				{
					output = "static const char " + line + "[7] = { \n\t";
					count = 0;
				}

				// Each information line starts with a zero.
				if (line.startsWith("0"))
				{
					value = 0;
					if (line.length() != 5)
					{
						System.err.println("Error while parsing matrix.");
					}
					else
					{
						for(int i=0; i<5; i++)
							value += (int)Math.pow(2,(4-i)) * ((int)line.charAt(i) - 48);

						/* One character contains 7 hex values. */
						count++;

						if ( count < 7)
							output += "0x" + ((Integer.toHexString(value).length() == 1) ? "0" : "") + Integer.toHexString(value) + ", ";

						if ( count == 7)
							output += "0x" + ((Integer.toHexString(value).length() == 1) ? "0" : "") + Integer.toHexString(value) + " };";
					}
				}

				/* Print the calculated line. */
				if (output.endsWith(";"))
					System.out.println(output);
			}
			br.close();
		}
		catch(IOException ioe)
		{
			System.err.println("Error while processing argv[0] = " + argv[0] + ": " + ioe.getMessage());
		}
	}
}