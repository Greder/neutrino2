/*
	Neutrino-GUI  -   DBoxII-Project
	
	$Id: textbox.h 2013/10/12 mohousch Exp $

	Homepage: http://dbox.cyberphoria.org/

	License: GPL

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
*/

#if !defined(TEXTBOX_H)
#define TEXTBOX_H

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string>
#include <vector>

#include <driver/gfx/framebuffer.h>
#include <driver/gfx/color.h>
#include <driver/gfx/fontrenderer.h>

#include <gui/widget/widget_helpers.h>


////
class CTextBox : public CComponent
{
	public:
		// mode
		enum
		{
			AUTO_WIDTH		= 0x01,
			AUTO_HIGH		= 0x02,
			SCROLL			= 0x04,
			CENTER			= 0x40,
			NO_AUTO_LINEBREAK 	= 0x80
		};

		// pic
		enum 
		{
			PIC_RIGHT,
			PIC_LEFT,
			PIC_CENTER
		};

	private:
		CFrameBuffer* frameBuffer;
		
		//
		CBox m_cFrameTextRel;
		CBox m_cFrameScrollRel;
		
		CCScrollBar scrollBar;

		// variables
		std::string m_cText;
		std::vector<std::string> m_cLineArray;

		int m_nMode;
		int m_tMode;

		int m_nNrOfPages;
		int m_nNrOfLines;
		int m_nNrOfNewLine;
		int m_nMaxLineWidth;
		int m_nLinesPerPage;
		int m_nCurrentLine;
		int m_nCurrentPage;

		// text
		unsigned int m_pcFontText;
		unsigned int m_nFontTextHeight;

		// backgrond
		fb_pixel_t m_textBackgroundColor;
		uint32_t m_textColor;
		int m_textRadius;
		int m_textCorner;
		//
		std::string thumbnail;
		int lx; 
		int ly; 
		int tw; 
		int th;
		bool enableFrame;
		bool bigFonts;
		bool painted;
		int borderMode;
		//
		fb_pixel_t* background;
		void saveScreen();
		void restoreScreen();

		// Functions
		void refreshTextLineArray(void);
		void initVar(void);
		void initFrames(void);
		void refreshScroll(void);
		void refreshText(void);
		void refreshPage(void);
		void reSizeTextFrameWidth(int maxTextWidth);
		void reSizeTextFrameHeight(int maxTextHeight);

	public:
		CTextBox(const int x = 0, const int y = 0, const int dx = MENU_WIDTH, const int dy = MENU_HEIGHT);
		CTextBox(CBox* position);

		virtual ~CTextBox();

		//// properties			
		bool setText(const char * const newText, const char * const _thumbnail = NULL, int _tw = 0, int _th = 0, int _tmode = PIC_RIGHT, bool enable_frame = false);
		void setPosition(const int x, const int y, const int dx, const int dy);
		void setPosition(const CBox * position);
		void setCorner(int ra, int co){m_textRadius = ra; m_textCorner = co;};
		void setBackgroundColor(fb_pixel_t col){m_textBackgroundColor = col;};
		void setTextColor(uint32_t col){m_textColor = col;};
		void setFont(unsigned int font_text){m_pcFontText = font_text;};
		void setMode(const int mode);
		void setBorderMode(int m = CComponent::BORDER_ALL){borderMode = m;};
		void setBigFonts();
		
		////
		void paint(void);
		void hide(void);
		inline bool isPainted(void){return painted;};
		
		//// events
		void scrollPageDown(const int pages = 1);
		void scrollPageUp(const int pages = 1);

		//// get methods
		inline int getMaxLineWidth(void){return(m_nMaxLineWidth);};
		inline int getLines(void){return(m_nNrOfLines);};
		inline int getPages(void){return(m_nNrOfPages);};
		inline void movePosition(int x, int y){itemBox.iX = x; itemBox.iY = y;};

		////
		bool isSelectable(void){return true;}
};

#endif // TEXTBOX_H

