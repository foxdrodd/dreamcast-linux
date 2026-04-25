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
 *  This program reads from a (special) file 192 bytes of data and displays
 *  them on a 48x32 virtual LCD display.
 *
 *  ChangeLog:
 *			  2003-10-13  Christian Berger (c.berger@tu-braunschweig.de)
 *						  Code cleanup.
 *
 *			  2003-05-03  Christian Berger (c.berger@tu-braunschweig.de)
 *						  Start of work.
 */

import java.io.BufferedReader;
import java.io.FileReader;
import java.io.IOException;

import java.awt.BorderLayout;
import java.awt.Canvas;
import java.awt.Color;
import java.awt.Dimension;
import java.awt.Graphics;
import java.awt.Graphics2D;
import java.awt.Toolkit;

import java.awt.event.ActionEvent;
import java.awt.event.ActionListener;
import java.awt.event.WindowEvent;
import java.awt.event.WindowListener;

import java.awt.geom.Rectangle2D;

import javax.swing.JButton;
import javax.swing.JFrame;
import javax.swing.JPanel;

/**
 * This class encapsulates a virual SEGA VMU LCD display.
 *
 * @author Christian Berger (c.berger@tu-braunschweig.de)
 * @version 0.01
 */
public class SEGA_VMU_Display extends JFrame implements ActionListener, WindowListener, Runnable
{
	public static void main(String argv[])
	{
		SEGA_VMU_Display display = new SEGA_VMU_Display((argv.length == 1) ? argv[0] : null);

		// Center window.
		Dimension d = Toolkit.getDefaultToolkit().getScreenSize();
		int x = (int) ((d.getWidth() - display.getWidth()) / 2);
		int y = (int) ((d.getHeight() - display.getHeight()) / 2);
		display.setLocation(x, y);
	}

	/*************************************************************/

	/** The wonderful Tux image :-) */
	private final int tuximg48x32[] = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00, 0x00, 
		0x00, 0x3f, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xc0, 0x88, 0x00, 0x00, 
		0x00, 0x3f, 0xe0, 0x80, 0x00, 0x00, 0x00, 0x6e, 0xe0, 0x8a, 0x94, 0xa0, 
		0x00, 0x5d, 0x60, 0x8b, 0x54, 0x40, 0x00, 0x5f, 0x60, 0x8a, 0x54, 0xa0, 
		0x00, 0x3f, 0xe0, 0xea, 0x4a, 0xa0, 0x00, 0x3f, 0xf0, 0x00, 0x00, 0x00, 
		0x00, 0x3f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x3e, 0x70, 0x00, 0x00, 0x00, 
		0x00, 0x40, 0x38, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x3c, 0x00, 0x00, 0x00, 
		0x01, 0x80, 0x3c, 0x00, 0x00, 0x00, 0x01, 0x80, 0x1e, 0x00, 0x00, 0x00, 
		0x03, 0x00, 0x1f, 0x00, 0x00, 0x00, 0x03, 0x00, 0x0f, 0x00, 0x00, 0x00, 
		0x02, 0x00, 0x0f, 0x80, 0x00, 0x00, 0x06, 0x00, 0x0f, 0x80, 0x00, 0x00, 
		0x06, 0x00, 0x0f, 0x80, 0x00, 0x00, 0x0e, 0x00, 0x0f, 0x80, 0x00, 0x00, 
		0x0e, 0x00, 0x0f, 0x80, 0x00, 0x00, 0x0f, 0x00, 0x1f, 0x80, 0x00, 0x00, 
		0x3f, 0x80, 0x3f, 0x80, 0x00, 0x00, 0x7f, 0xe0, 0x3f, 0xc0, 0x00, 0x00, 
		0x7f, 0xe0, 0x3f, 0xe0, 0x00, 0x00, 0x7f, 0xc0, 0x7f, 0xf0, 0x00, 0x00, 
		0xff, 0xe1, 0xff, 0xe0, 0x00, 0x00, 0xff, 0xff, 0xff, 0xc0, 0x00, 0x00, 
		0x3f, 0xff, 0xff, 0x00, 0x00, 0x00, 0x03, 0xe0, 0x3c, 0x00, 0x00, 0x00
	};

	/** GUI elements. */
	private JPanel panelButtons = null;
	private JButton draw = null;
	private JButton quit = null;
	private LCD_Canvas canvas = null;

	private String PIPE_NAME = "";

	private boolean bitarray[][];

	public SEGA_VMU_Display(String NAMED_PIPE)
	{
		super("SEGA VMU - Display");
		addWindowListener(this);

		if ( (PIPE_NAME = NAMED_PIPE) != null )
			(new Thread(this)).start();

		setSize(500, 400);

		// LayoutManager.
		getContentPane().setLayout(new BorderLayout());

		panelButtons = new JPanel();
			draw = new JButton("Draw sample.");
			draw.addActionListener(this);
		panelButtons.add(draw);
			quit = new JButton("Quit LCD display.");
			quit.addActionListener(this);
		panelButtons.add(quit);

		canvas = new LCD_Canvas();

		getContentPane().add(BorderLayout.CENTER, canvas);
		getContentPane().add(BorderLayout.SOUTH, panelButtons);

		// Initialize display.
		bitarray = new boolean[48][32];
		for(int j = 0; j < 32; j++)
			for(int i = 0; i < 48; i++)
				bitarray[i][j] = false;

		setVisible(true);
	}

	/**
	 * Function declared in interface Runnable for implementing threading capabilities.
	 */
	public void run()
	{
		String from_user = "";
		BufferedReader PIPE = null;
		int pos = -1;

		try
		{
			while (true)
			{
				pos = -1;
				if (PIPE_NAME != null && PIPE_NAME != "")
					PIPE = new BufferedReader(new FileReader(PIPE_NAME));

				while ((from_user = PIPE.readLine()) != null)
				{
					if (from_user.length() != 192)
						System.err.println("Error while reading NAMED_PIPE: Not enough bytes read (" + from_user.length() + "/192)");
					else
					{
						System.out.println("Read 192 bytes:\n\"" + from_user + "\"");

						/* Now, paint 192 bytes on the virtual display. */
						for(int j = 0; j < 32; j++)
						{
							for(int i = 0; i < 48; i++)
							{
								if( (i%8) == 0 )
									pos++;

								/* Store the read values into a bit array. */
								bitarray[i][j] = !((from_user.charAt(pos) & (int)Math.pow(2,7-(i%8))) == 0);
							}
						}

						/* Repaint display. */
						canvas.repaint();
					}
				}
			}
		}
		catch (IOException ioe)
		{
			System.err.println("Error while using NAMED_PIPE. " + ioe.getMessage());
		}
		System.err.println("Thread finished.");
	}

	/*************************************************************/

	/** The LCD display canvas. */
	class LCD_Canvas extends Canvas 
	{
		public LCD_Canvas() 
		{
			setBackground(Color.gray);
		}

		public void paint(Graphics g) 
		{
			Graphics2D g2D = (Graphics2D) g;

			for(int j = 0; j < 32; j++)
			{
				for(int i = 0; i < 48; i++)
				{
					Rectangle2D.Float rect = new Rectangle2D.Float(10+10*i, 10+10*j, 5, 5);
					g2D.setColor((bitarray[i][j]) ? Color.darkGray : Color.green);
					g2D.fill(rect);
					g2D.draw(rect);
				}
			}
		}
	}

	/*************************************************************/

	public void windowActivated(WindowEvent e){}
	public void windowDeactivated(WindowEvent e){}
	public void windowClosed(WindowEvent e){}
	public void windowClosing(WindowEvent e)
	{
		System.exit(0);
	}
	public void windowDeiconified(WindowEvent e){}
	public void windowIconified(WindowEvent e){}
	public void windowOpened(WindowEvent e){}

	/**
	 * Function for reacting on user events.
	 */
	public void actionPerformed(ActionEvent e)
	{
		int pos = -1;
		if(e.getActionCommand().toString().indexOf("Draw") >= 0)
		{
			for(int j = 0; j < 32; j++)
			{
				for(int i = 0; i < 48; i++)
				{
					if( (i%8) == 0 )
						pos++;

					bitarray[i][j] = !((tuximg48x32[pos] & (int)Math.pow(2,7-(i%8))) == 0);
				}
			}

			/* Repaint display. */
			canvas.repaint();
		}

		if(e.getActionCommand().toString().indexOf("Quit") >= 0)
			System.exit(0);
	}
}