/****************************************************************************
* libFreeType
*
* Needed to show the user what's the hell's going on !!
* These functions are generic, and do not contain any Memory Card specific
* routines.
****************************************************************************/
#include <gccore.h>
#include <ogcsys.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include "bannerload.h"
#include "freetype.h"
#include "gci.h"
#include "mcard.h"
#include "sdsupp.h"
#include "bitmap.h"
#include "raw.h"

#ifdef HW_RVL
#include <wiiuse/wpad.h>
#endif

#define MCDATAOFFSET 64
#define FONT_SIZE 16 //pixels

/*** Globals ***/
bool offsetchanged = true;
FT_Library ftlibrary;
FT_Face face;
FT_GlyphSlot slot;
FT_UInt glyph_index;
extern card_stat CardStatus;
extern int cancel;
extern int mode;
extern s32 MEM_CARD;
extern u16 bannerdata[CARD_BANNER_W*CARD_BANNER_H] ATTRIBUTE_ALIGN (32);
extern u8 bannerdataCI[CARD_BANNER_W*CARD_BANNER_H] ATTRIBUTE_ALIGN (32);
extern u8 icondata[1024] ATTRIBUTE_ALIGN (32);
extern u16 icondataRGB[1024] ATTRIBUTE_ALIGN (32);
extern u16 tlut[256] ATTRIBUTE_ALIGN (32);
extern u16 tlutbanner[256] ATTRIBUTE_ALIGN (32);
extern Header cardheader;
extern s32 memsize, sectsize;
extern syssramex *sramex;
extern u8 imageserial[12];

static int fonthi, fontlo;

/*** The actual TrueType Font ***/
extern char fontface[];		/*** From fontface.s ***/
extern int fontsize;			/*** From fontface.s ***/

/*** From video subsystem ***/
extern int screenheight;
extern u32 *xfb[2];
extern int whichfb;
extern GXRModeObj *vmode;

/*** File listings ***/
extern card_dir CardList[];
extern u8 filelist[1024][1024];
extern u32 maxfile;
extern GCI gci;
#define MAXFILEBUFFER (1024 * 2048)
extern u8 FileBuffer[MAXFILEBUFFER] ATTRIBUTE_ALIGN (32);
extern u8 CommentBuffer[64] ATTRIBUTE_ALIGN (32);

#define PAGESIZE 16


#ifdef HW_RVL
// POWER BUTTON
bool power=false;

void PowerOff()
{
	//LWP_MutexDestroy(m_audioMutex);
	SYS_ResetSystem(SYS_POWEROFF, 0, 0);
	exit(0);
}

static void set_power_var()
{
	power = true;
}
static void set_wiimote_power_var(s32 chan)
{
	power = true;
}
void initialise_power()
{
	SYS_SetPowerCallback(set_power_var);
	WPAD_SetPowerButtonCallback(set_wiimote_power_var);
}
// END POWER BUTTON
#endif

/****************************************************************************
* FT_Init
*
* Initialise freetype library
****************************************************************************/
int FT_Init ()
{

	int err;

	err = FT_Init_FreeType (&ftlibrary);
	if (err)
		return 1;

	err =
	    FT_New_Memory_Face (ftlibrary, (FT_Byte *) fontface, fontsize, 0, &face);
	if (err)
		return 1;

	setfontsize (FONT_SIZE);
	setfontcolour (0xff, 0xff, 0xff);

	slot = face->glyph;

	return 0;

}

/****************************************************************************
* setfontsize
****************************************************************************/
void setfontsize (int pixelsize)
{
	int err;

	err = FT_Set_Pixel_Sizes (face, 0, pixelsize);

	if (err)
		printf ("Error setting pixel sizes!");
}

static void DrawCharacter (FT_Bitmap * bmp, FT_Int x, FT_Int y)
{
	FT_Int i, j, p, q;
	FT_Int x_max = x + bmp->width;
	FT_Int y_max = y + bmp->rows;
	int spos;
	unsigned int pixel;
	int c;

	for (i = x, p = 0; i < x_max; i++, p++)
	{
		for (j = y, q = 0; j < y_max; j++, q++)
		{
			if (i < 0 || j < 0 || i >= 640 || j >= screenheight)
				continue;

			/*** Convert pixel position to GC int sizes ***/
			spos = (j * 320) + (i >> 1);

			pixel = xfb[whichfb][spos];
			c = bmp->buffer[q * bmp->width + p];

			/*** Cool Anti-Aliasing doesn't work too well at hires on GC ***/
			if (c > 128)
			{
				if (i & 1)
					pixel = (pixel & 0xffff0000) | fontlo;
				else
					pixel = ((pixel & 0xffff) | fonthi);

				xfb[whichfb][spos] = pixel;
			}
		}
	}
}

/**
 * DrawText
 *
 * Place the font bitmap on the screen
 */
void DrawText (int x, int y, char *text)
{
	int px, n;
	int i;
	int err;
	int value, count;

	n = strlen (text);
	if (n == 0)
		return;

	/*** x == -1, auto centre ***/
	if (x == -1)
	{
		value = 0;
		px = 0;
	}
	else
	{
		value = 1;
		px = x;
	}

	for (count = value; count < 2; count++)
	{
		/*** Draw the string ***/
		for (i = 0; i < n; i++)
		{
			err = FT_Load_Char (face, text[i], FT_LOAD_RENDER);

			if (err)
			{
				printf ("Error %c %d\n", text[i], err);
				continue;				/*** Skip unprintable characters ***/
			}

			if (count)
				DrawCharacter (&slot->bitmap, px + slot->bitmap_left,
				               y - slot->bitmap_top);

			px += slot->advance.x >> 6;
		}

		px = (640 - px) >> 1;

	}
}

/**
 * getcolour
 *
 * Simply converts RGB to Y1CbY2Cr format
 *
 * I got this from a pastebin, so thanks to whoever originally wrote it!
 */

static unsigned int getcolour (u8 r1, u8 g1, u8 b1)
{
	int y1, cb1, cr1, y2, cb2, cr2, cb, cr;
	u8 r2, g2, b2;

	r2 = r1;
	g2 = g1;
	b2 = b1;

	y1 = (299 * r1 + 587 * g1 + 114 * b1) / 1000;
	cb1 = (-16874 * r1 - 33126 * g1 + 50000 * b1 + 12800000) / 100000;
	cr1 = (50000 * r1 - 41869 * g1 - 8131 * b1 + 12800000) / 100000;

	y2 = (299 * r2 + 587 * g2 + 114 * b2) / 1000;
	cb2 = (-16874 * r2 - 33126 * g2 + 50000 * b2 + 12800000) / 100000;
	cr2 = (50000 * r2 - 41869 * g2 - 8131 * b2 + 12800000) / 100000;

	cb = (cb1 + cb2) >> 1;
	cr = (cr1 + cr2) >> 1;

	return ((y1 << 24) | (cb << 16) | (y2 << 8) | cr);
}

/**
 * setfontcolour
 *
 * Uses RGB triple values.
 */
void setfontcolour (u8 r, u8 g, u8 b)
{
	u32 fontcolour;

	fontcolour = getcolour (r, g, b);
	fonthi = fontcolour & 0xffff0000;
	fontlo = fontcolour & 0xffff;
}

/****************************************************************************
* ClearScreen
****************************************************************************/
/*void ClearScreen (){
  whichfb ^= 1;
  VIDEO_ClearFrameBuffer (vmode, xfb[whichfb], COLOR_BLACK);
}
*/
/****************************************************************************
* ShowScreen
****************************************************************************/
void ShowScreen ()
{
	VIDEO_SetNextFramebuffer (xfb[whichfb]);
	VIDEO_Flush ();
	VIDEO_WaitVSync ();
}

/**
 * Show an action in progress
 */
void ShowAction (char *msg)
{
	//int ypos = screenheight >> 1;
	//ypos += 16;

	//ClearScreen ();
	//DrawText (-1, ypos, msg);
	writeStatusBar(msg,"");
	ShowScreen ();
}

/****************************************************************************
* SelectMode
****************************************************************************/
int SelectMode ()
{
	/*
	int ypos;

	ypos = (screenheight >> 1);

	ClearScreen ();
	DrawText (-1, ypos, "Press A for SMB BACKUP mode");
	DrawText (-1, ypos + 20, "Press B for SMB RESTORE mode");
	DrawText (-1, ypos + 40, "Press Y for SD CARD BACKUP mode");
	DrawText (-1, ypos + 60, "Press X for SD CARD RESTORE mode");
	DrawText (-1, ypos + 80, "Press Z for SD/PSO Reload");
	ShowScreen ();*/
	ClearScreen ();
	//setfontcolour(84,174,211);
// setfontcolour(28,28,28);

// DrawText(495,305,"choose your mode");
	writeStatusBar("Choose your mode","");
	ShowScreen();
	/*** Clear any pending buttons - button 'debounce' ***/
	while (PAD_ButtonsHeld (0)
#ifdef HW_RVL
	        | WPAD_ButtonsHeld(0)
#endif
	      )
		VIDEO_WaitVSync ();

	for (;;)
	{

		if (PAD_ButtonsHeld (0) & PAD_BUTTON_A)
		{
			while ((PAD_ButtonsDown (0) & PAD_BUTTON_A))
			{
				VIDEO_WaitVSync ();
			}
			return 100;
		}
		if (PAD_ButtonsHeld (0) & PAD_TRIGGER_Z)//Delete mode
		{
			while ((PAD_ButtonsDown (0) & PAD_BUTTON_B))
			{
				VIDEO_WaitVSync ();
			}
			return 200;
		}
		if (PAD_ButtonsHeld (0) & PAD_BUTTON_Y)//Backup mode
		{
			while ((PAD_ButtonsDown (0) & PAD_BUTTON_Y))
			{
				VIDEO_WaitVSync ();
			}
			return 300;
		}
		if (PAD_ButtonsHeld (0) & PAD_BUTTON_X)//Restore mode
		{
			while ((PAD_ButtonsDown (0) & PAD_BUTTON_X))
			{
				VIDEO_WaitVSync ();
			}
			return 400;
		}
		if (PAD_ButtonsHeld (0) & PAD_BUTTON_START)//Exit
		{
			while ((PAD_ButtonsDown (0) & PAD_BUTTON_START))
			{
				VIDEO_WaitVSync ();
			}
			return 500;
		}
		if (PAD_ButtonsHeld (0) & PAD_TRIGGER_R)//Backup all mode
		{
			while ((PAD_ButtonsDown (0) & PAD_TRIGGER_R))
			{
				VIDEO_WaitVSync ();
			}
			return 600;
		}
		while (PAD_ButtonsHeld (0) & PAD_TRIGGER_L)
		{
			if (PAD_ButtonsHeld (0) & PAD_BUTTON_Y){//Raw backup mode
				while ((PAD_ButtonsDown (0) & PAD_TRIGGER_L) || (PAD_ButtonsDown (0) & PAD_BUTTON_Y))
				{
					VIDEO_WaitVSync ();
				}
				return 700;
			}

			if (PAD_ButtonsHeld (0) & PAD_BUTTON_X){//Raw restore mode
				while ((PAD_ButtonsDown (0) & PAD_TRIGGER_L) || (PAD_ButtonsDown (0) & PAD_BUTTON_X))
				{
					VIDEO_WaitVSync ();
				}
				return 800;
			}

			if (PAD_ButtonsHeld (0) & PAD_TRIGGER_Z){//Format card mode
				while ((PAD_ButtonsDown (0) & PAD_TRIGGER_L) || (PAD_ButtonsDown (0) & PAD_TRIGGER_Z))
				{
					VIDEO_WaitVSync ();
				}
				return 900;
			}			
		}		
#ifdef HW_RVL
		if (WPAD_ButtonsHeld (0) & WPAD_BUTTON_A)
		{
			while ((WPAD_ButtonsDown (0) & WPAD_BUTTON_A))
			{
				VIDEO_WaitVSync ();
			}
			return 100;
		}
		if (WPAD_ButtonsHeld (0) & WPAD_BUTTON_2)
		{
			while ((WPAD_ButtonsDown (0) & WPAD_BUTTON_2))//Delete mode
			{
				VIDEO_WaitVSync ();
			}
			return 200;
		}
		if (WPAD_ButtonsHeld (0) & WPAD_BUTTON_MINUS)//Backup mode
		{
			while ((WPAD_ButtonsDown (0) & WPAD_BUTTON_MINUS))
			{
				VIDEO_WaitVSync ();
			}
			return 300;
		}
		if (WPAD_ButtonsHeld (0) & WPAD_BUTTON_PLUS)//Restore mode
		{
			while ((WPAD_ButtonsDown (0) & WPAD_BUTTON_PLUS))
			{
				VIDEO_WaitVSync ();
			}
			return 400;
		}
		if (WPAD_ButtonsHeld (0) & WPAD_BUTTON_HOME)//Exit
		{
			while ((WPAD_ButtonsDown (0) & WPAD_BUTTON_HOME))
			{
				VIDEO_WaitVSync ();
			}
			return 500;
		}
		if (WPAD_ButtonsHeld (0) & WPAD_BUTTON_1)//Backup all mode
		{
			while ((WPAD_ButtonsDown (0) & WPAD_BUTTON_1))
			{
				VIDEO_WaitVSync ();
			}
			return 600;
		}
		while (WPAD_ButtonsHeld (0) & WPAD_BUTTON_B)
		{
			if (WPAD_ButtonsHeld (0) & WPAD_BUTTON_MINUS){//Raw backup mode
				while ((WPAD_ButtonsDown (0) & WPAD_BUTTON_B) || (WPAD_ButtonsDown (0) & WPAD_BUTTON_MINUS))
				{
					VIDEO_WaitVSync ();
				}
				return 700;
			}

			if (WPAD_ButtonsHeld (0) & WPAD_BUTTON_PLUS){//Raw restore mode
				while ((WPAD_ButtonsDown (0) & WPAD_BUTTON_B) || (WPAD_ButtonsDown (0) & WPAD_BUTTON_PLUS))
				{
					VIDEO_WaitVSync ();
				}
				return 800;
			}
			
			if (WPAD_ButtonsHeld (0) & WPAD_BUTTON_2){//Format card mode
				while ((WPAD_ButtonsDown (0) & WPAD_BUTTON_B) || (WPAD_ButtonsDown (0) & WPAD_BUTTON_2))
				{
					VIDEO_WaitVSync ();
				}
				return 900;
			}			
		}
		if (power)
		{
			PowerOff();
		}
#endif

		VIDEO_WaitVSync ();
	}
}

/**
 * Wait for user to press A
 */
void WaitButtonA ()
{
#ifdef HW_DOL
	while (PAD_ButtonsDown (0) & PAD_BUTTON_A );
	while (!(PAD_ButtonsDown (0) & PAD_BUTTON_A));
#else
	while (1)
	{
		if (PAD_ButtonsDown (0) & PAD_BUTTON_A )
			break;
		if (WPAD_ButtonsDown (0) & WPAD_BUTTON_A )
			break;
		if (power)
		{
			PowerOff();
		}
	}
	while (1)
	{
		if (!(PAD_ButtonsDown (0) & PAD_BUTTON_A))
			break;
		if (!(WPAD_ButtonsDown (0) & WPAD_BUTTON_A))
			break;
		if (power)
		{
			PowerOff();
		}
	}
#endif
}

/****************************************************************************
 * Wait for user to press A or B. Returns 0 = B; 1 = A
 ****************************************************************************/

int WaitButtonAB ()
{
#ifdef HW_RVL
	u32 gc_btns, wm_btns;

	while ( (PAD_ButtonsDown (0) & (PAD_BUTTON_A | PAD_BUTTON_B))
	        || (WPAD_ButtonsDown(0) & (WPAD_BUTTON_A | WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_A | WPAD_CLASSIC_BUTTON_B))
	      ) VIDEO_WaitVSync();

	while (1)
	{
		gc_btns = PAD_ButtonsDown (0);
		wm_btns = WPAD_ButtonsDown (0);
		if ( (gc_btns & PAD_BUTTON_A) || (wm_btns & (WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A)) )
			return 1;
		else if ( (gc_btns & PAD_BUTTON_B) || (wm_btns & (WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B)) )
			return 0;
	}
#else
	u32 gc_btns;

	while ( (PAD_ButtonsDown (0) & (PAD_BUTTON_A | PAD_BUTTON_B)) ) VIDEO_WaitVSync();

	while (1)
	{
		gc_btns = PAD_ButtonsDown (0);
		if ( gc_btns & PAD_BUTTON_A )
			return 1;
		else if ( gc_btns & PAD_BUTTON_B )
			return 0;
	}
#endif
}

/****************************************************************************
 * Wait for user to press A or B. Returns 0 = Z/2; 1 = A
 ****************************************************************************/

int WaitButtonAZ ()
{
#ifdef HW_RVL
	u32 gc_btns, wm_btns;

	while ( (PAD_ButtonsDown (0) & (PAD_BUTTON_A | PAD_TRIGGER_Z))
	        || (WPAD_ButtonsDown(0) & (WPAD_BUTTON_A | WPAD_BUTTON_2 | WPAD_CLASSIC_BUTTON_A | WPAD_CLASSIC_BUTTON_ZR | WPAD_CLASSIC_BUTTON_ZL))
	      ) VIDEO_WaitVSync();

	while (1)
	{
		gc_btns = PAD_ButtonsDown (0);
		wm_btns = WPAD_ButtonsDown (0);
		if ( (gc_btns & PAD_BUTTON_A) || (wm_btns & (WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A)) )
			return 1;
		else if ( (gc_btns & PAD_TRIGGER_Z) || (wm_btns & (WPAD_BUTTON_2 | WPAD_CLASSIC_BUTTON_ZR | WPAD_CLASSIC_BUTTON_ZL)) )
			return 0;
	}
#else
	u32 gc_btns;

	while ( (PAD_ButtonsDown (0) & (PAD_BUTTON_A | PAD_TRIGGER_Z)) ) VIDEO_WaitVSync();

	while (1)
	{
		gc_btns = PAD_ButtonsDown (0);
		if ( gc_btns & PAD_BUTTON_A )
			return 1;
		else if ( gc_btns & PAD_TRIGGER_Z )
			return 0;
	}
#endif
}


/**
 * Show a prompt
 */
void WaitPrompt (char *msg)
{
	//int ypos = (screenheight) >> 1;
	//ClearScreen ();
	//DrawText (-1, ypos, msg);
	//ypos += 20;
	//DrawText (-1, ypos, "Press A to continue");
	writeStatusBar(msg,"Press A to continue");
	ShowScreen ();
	WaitButtonA ();
	//clear the text
	writeStatusBar("","");

}

/****************************************************************************
 * Show a prompt with choice of two options. Returns 1 if A button was pressed
   and 0 if B button was pressed.
 ****************************************************************************/
int WaitPromptChoice ( char *msg, char *bmsg, char *amsg)
{
	char txt[80];
	int ret;
	sprintf (txt, "B = %s   :   A = %s", bmsg, amsg);
	writeStatusBar(msg, txt);
	ret = WaitButtonAB ();
	//Clear the text
	writeStatusBar("","");

	return ret;
}
/****************************************************************************
 * Show a prompt with choice of two options. Returns 1 if A button was pressed
   and 0 if Z/2 button was pressed.
 ****************************************************************************/
int WaitPromptChoiceAZ ( char *msg, char *bmsg, char *amsg)
{
	char txt[80];
	int ret;
#ifdef	HW_RVL
	sprintf (txt, "Z/2 = %s   :   A = %s", bmsg, amsg);
#else
	sprintf (txt, "Z = %s   :   A = %s", bmsg, amsg);
#endif
	writeStatusBar(msg, txt);
	ret = WaitButtonAZ ();
	//Clear the text
	writeStatusBar("","");

	return ret;
}
void DrawLineFast (int startx, int endx, int y, u8 r, u8 g, u8 b)
{
	int width;
	u32 offset;
	u32 colour;
	int i;

	offset = (y * 320) + (startx >> 1);
	colour = getcolour (r, g, b);
	width = (endx - startx) >> 1;

	for (i = 0; i < width; i++)
		xfb[whichfb][offset++] = colour;
}

void showCardInfo(int sel){
	//clear right pane, but just the card info
	int bgcolor = getcolour(84,174,211);
	DrawBoxFilled(375, 165, 605, 390, bgcolor);
	int y = 190, x = 378;
	int err;
	char txt[1024];
	int i;
	char temp[5];

	//START card image info START
	//put block 1 in cardheader
	SDLoadCardImageHeader((char*)filelist[sel]);
	//get the serial
	getserial(imageserial);
	
	sprintf(txt, "Card image flash ID");
	DrawText(x, y, txt);
	y += 20;	

	sprintf(txt, "%02X", imageserial[0]);
	for (i=1; i<11; i++){
		sprintf(temp, "%02X", imageserial[i]);
		strcat(txt, temp);
	}
	DrawText(x,y,txt);
	y+=20;
	
	sprintf(txt, "Size: %d blocks", (cardheader.SizeMb[1]*16)-5);
	DrawText(x, y, txt);
	y+=40;
	//END card image info END

	//START real card info START
	err = MountCard(MEM_CARD);
	if (err < 0)
	{
		WaitCardError("CardMount", err);
		return;			/*** Unable to mount the card ***/
	}
	
	sramex = __SYS_LockSramEx();
	__SYS_UnlockSramEx(0);
	
	if (!MEM_CARD)
	{
		sprintf(txt, "Slot A card flash ID:");
	}else
	{
		sprintf(txt, "Slot B card flash ID:");
	}
	DrawText(x,y,txt);
	y+=20;

	sprintf(txt, "%02X", sramex->flash_id[MEM_CARD][0]);
	for (i=1; i<sizeof(sramex->flash_id[MEM_CARD])-1; i++){
		sprintf(temp, "%02X", sramex->flash_id[MEM_CARD][i]);
		strcat(txt, temp);
	}
	DrawText(x,y,txt);
	y+=20;
	
	sprintf(txt, "Size: %d blocks", (memsize*16)-5);
	DrawText(x,y,txt);
	//END real card info END
}

void showSaveInfo(int sel)
{
	int y = 160, x = 378, j;
	char gamecode[5], company[3], txt[1024];

	//clear right pane, but just the save info
	int bgcolor = getcolour(84,174,211);
	DrawBoxFilled(375, 145, 605, 390, bgcolor);

	// read file, display some more info
	// TODO: only read the necessary header + comment, display banner and icon files
	if (mode == RESTORE_MODE)
	{
		j = SDLoadMCImageHeader((char*)filelist[sel]);
		// struct gci now contains header info
		memcpy(company, gci.company, 2);
		memcpy(gamecode, gci.gamecode, 4);
		company[2] = gamecode[4] = 0;

	}
	else if( (mode == BACKUP_MODE) | (mode == DELETE_MODE) )
	{
		memcpy(company, CardList[sel].company, 2);
		memcpy(gamecode, CardList[sel].gamecode, 4);
		//null terminate gamecode and company
		company[2] = gamecode[4] = 0;
		j = CardReadFileHeader(MEM_CARD, sel);
		// struct gci now contains header info

	}

	//Show icon and banner
	if ((gci.banner_fmt&CARD_BANNER_MASK) == CARD_BANNER_RGB) {
		bannerloadRGB(bannerdata);
	}
	else if ((gci.banner_fmt&CARD_BANNER_MASK) == CARD_BANNER_CI) {
		bannerloadCI(bannerdataCI, tlutbanner);
	}
	if (gci.icon_fmt&0x01) {
		iconloadCI(icondata, tlut);
	}
	else {
		iconloadRGB(icondataRGB);
	}
    
	/*** Display relevant info for this save ***/
	sprintf(txt, "#%d %s/%s", sel, gamecode, company);
	DrawText(x, y, txt);
	y += 60;

	//DrawText(x, y, "File Description:");
	//y += 20;

	char comment1[26];
	char comment2[6];
	
	//We go ahead and print the full 32byte comment lines - could go offscreen
	//but we like to live dangerously
	for (j = 0; j < 32; j++) {
		if ((char)CommentBuffer[j] == 0 || (char)CommentBuffer[j] == 10) {break;}
		comment1[j] = (char)CommentBuffer[j];
	}
	comment1[j] = 0;
	DrawText(x, y, comment1);
	y += 20;
	memset(comment1, 0, sizeof(comment1));
	
	for (j = 32; j < 64; j++) {
		if ((char)CommentBuffer[j] == 0 || (char)CommentBuffer[j] == 10) {break;}
		comment2[j-32] = (char)CommentBuffer[j];
	}
	comment2[j-32] = 0;
	DrawText(x, y, comment2);
	y += 20;
	memset(comment2, 0, sizeof(comment2));
	y += 20;

	// Compute date/time
	u32 t = gci.time;  // seconds since jan 1, 2000
	/*
	//Raw time display
	sprintf(txt, "D: %08X", t);
	DrawText(x, y, txt);	
	y += 20;
	*/

	t += 946684800;	//let's add seconds since Epoch (jan 1, 1970 to jan 1,2000)
	int month, day, year, hour, min, sec;
	month = day = year = hour = min = sec = 0;
	char monthstr[12][4] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul",
	                         "Aug", "Sep", "Oct", "Nov", "Dec"
	                       };

	// Taken from void SecondsToDate(int seconds, int *pYear, int *pMonth, int *pDay)
	// Calculates year month and day since jan 1, 1970 from (t) seconds
	// Reference: Fliegel, H. F. and van Flandern, T. C. (1968).
	// Communications of the ACM, Vol. 11, No. 10 (October, 1968).
	// Original code in Fortran
		int I, J, K, L, N;

		L = t / 86400 + 2509157;
		N = 4 * L / 146097;
		L = L - (146097 * N + 3) / 4;
		I = 4000 * (L + 1) / 1461001;
		L = L - 1461 * I / 4 + 31;
		J = 80 * L / 2447;
		K = L - 2447 * J / 80;
		L = J / 11;
		J = J + 2 - 12 * L;
		I = 100 * (N - 49) + I + L;
		year = I;
		month = J;
		day = K;
		
	sec = t % 60;
	t /= 60;
	min = t % 60;
	t /= 60;
	hour = t % 24;

	sprintf(txt, "Date: %s %02d, %04d", monthstr[month-1], day, year);
	DrawText(x, y, txt);
	y += 20;
	sprintf(txt, "Time: %02d:%02d:%02d", hour, min, sec);
	DrawText(x, y, txt);
	y += 20;
	sprintf(txt, "Blocks: %d", gci.filesize8);
	DrawText(x, y, txt);
	y += 20;
	//M-> Cant' move the file //C->can't copy the file //P->public file //Most frecuent: xxP
	sprintf(txt, "Perm: %s%s%s",
	        (gci.unknown1 & 16) ? "M " : "x ",
	        (gci.unknown1 & 8) ? "C " : "x ",
	        (gci.unknown1 & 4) ? "P" : "x");
	DrawText(x, y, txt);
	y += 20;
	sprintf(txt, "Copy Count: %02d", gci.unknown2);
	DrawText(x, y, txt);
	//y += 20;

	/*** Uncomment to print some debug info ***/
	//y+=40;
	//sprintf(comment2, "%x %x %x", gci.icon_addr, gci.icon_fmt, gci.banner_fmt);
	//DrawText(x, y, comment2);

}

// saveinfo 1: shows saveinfo (for gci/sav/gcs backup/restore)
// saveinfo 0: retrieves .raw, .gcp and .mci
static void ShowFiles (int offset, int selection, int upordown, int saveinfo) {
	int i, j;    //j helps us determine which entry to highlight
	char text[23];
	int ypos;
	int w;
	//int textlen = 22;
	int textlen = 32;
	//If the filelist offset changed, we have to redraw every entry
	//This tracks switching "pages" of files in the list
	if (offsetchanged) {
		//Must set this or we always redraw all files
		offsetchanged = false;
		//clear entire left side since we want to update all
		clearLeftPane();
		ShowScreen();
		setfontsize (14);
		setfontcolour (0xff, 0xff, 0xff);
		//Do a little math (480 - (16*20+40))/2
		//(ypos = 60 seems perfect given the current background bmp)
		ypos = (screenheight - (PAGESIZE * 20+40)) >> 1;
		ypos += 26;
		j = 0;
		for (i = offset; i < (offset + PAGESIZE) && (i < maxfile); i++){
			//changed this to limit characters shown in filename since display gets weird
			//if we let the entire filename appear
			strncpy (text, (char*)filelist[i], textlen);
			text[textlen] = 0;
			if (j == (selection - offset)){
				/*** Highlighted text entry ***/
				for (w = 0; w < 20; w++){
					//Draw white lines to highlight this area.
					DrawLineFast (35, 330, (j * 20) + (ypos - 14) + w, 0xff, 0xff, 0xff);
				}
				setfontcolour (28, 28, 28);
				DrawText (35, (j * 20) + ypos, text); 
				setfontcolour (0xff, 0xff, 0xff);
			}
			else{
				/*** Normal entry ***/
				//****************
				for (w = 0; w < 20; w++){
					DrawLineFast (35, 330, (j * 20) + (ypos - 14) + w, 84, 174,211);
				//***********************************************************
				}
				DrawText (35, (j * 20) + ypos, text);
			}
			j++;
		}
		//set black font - info on right side of screen is printed in showSaveInfo
		setfontcolour (28, 28, 28);
		if (saveinfo){
			showSaveInfo(selection);
		}else if (!saveinfo){
			showCardInfo(selection);
		}
	}
	else {
		//Filelist offset didn't change yet; Only redraw what matters
		setfontsize (14);
		ypos = (screenheight - (PAGESIZE * 20+40)) >> 1;
		ypos += 26;
		setfontcolour (0xff, 0xff, 0xff);
		//user just pressed up
		if (upordown == 1) {
			strncpy (text, (char*)filelist[selection+1], textlen);
			text[textlen] = 0;
			for (w = 0; w < 20; w++){
				DrawLineFast (35, 330, (((selection-offset)+1) * 20) + (ypos - 14) + w, 84, 174,211);
			}
			DrawText (35, (((selection-offset)+1) * 20) + ypos, text);
		}
		//user just pressed down
		else if (upordown == 2) {
			strncpy (text, (char*)filelist[selection-1], textlen);
			text[textlen] = 0;
			for (w = 0; w < 20; w++){
				DrawLineFast (35, 330, (((selection-offset)-1) * 20) + (ypos - 14) + w, 84, 174,211);
			}
			DrawText (35, (((selection-offset)-1) * 20) + ypos, text);
		}
		//Important to call ShowScreen here if we want the redraw to
		//appear faster - without it the highlight box glides too much
		ShowScreen();
		setfontcolour (28, 28, 28);
		strncpy (text, (char*)filelist[selection], textlen);
		text[textlen] = 0;
		//the current spot is always highlighted
		for (w = 0; w < 20; w++){
			DrawLineFast (35, 330, ((selection-offset) * 20) + (ypos - 14) + w, 0xff, 0xff, 0xff);
		}
		DrawText (35, ((selection-offset) * 20) + ypos, text);
		if (saveinfo){
			showSaveInfo(selection);
		}else if (!saveinfo){
			showCardInfo(selection);
		}
	}
	//Need this to show info update from showSaveInfo
	ShowScreen();
}

/*void ShowFiles(int offset, int selected, int maxfiles) {
    int y = 110, tmp;

    //WaitPrompt("ShowFiles");
    clearLeftPane();
    for (tmp = offset; (tmp<offset+PAGESIZE) && (tmp < maxfiles); tmp++) {
        char str[1024], txt[1024];
        int x, t;
        strcpy(str, CardList[tmp].filename);
        for (x=t=0; t<strlen(str); t++) {
            x += font_size[(int)str[t]];
            if (x > 290) { // max width of left pane
                str[t] = 0;
                t = strlen(str);
            }
        }
        strcpy(txt, str);
        //txt[t-1] = 0;
        if (tmp == selected) {
            // Highlight this file
            fntDrawBoxFilled(35, y, 330, y+23, hlcolor);
			showSaveInfo(selected);
        }
        write_font(35, y, txt);  // write filename on left
        y += 20;
    }
}
*/


/****************************************************************************
* ShowSelector
*
* Let's the user select a file to backup or restore.
* Returns index of file chosen.
* saveinfo 1: enables getting and displaying saveinformation
* saveinfo 0: disables save information (for raw mode)
****************************************************************************/
int ShowSelector (int saveinfo)
{
	u32 p;
#ifdef HW_RVL
	u32 wp;
#endif
	int upordown = 0; //pass this to ShowFiles
	int redraw = 1; //determines need for screen update
	int quit = 0;
	int offset = 0;//further determines redraw conditions
	int selection = 0;

	while (quit == 0)
	{
		if (redraw)
		{
			ShowFiles (offset, selection, upordown, saveinfo);
			redraw = 0;
		}
		p = PAD_ButtonsDown (0);
#ifdef HW_RVL
		wp = WPAD_ButtonsDown (0);
#endif
		if ((p & PAD_BUTTON_B )
#ifdef HW_RVL
		        | (wp & WPAD_BUTTON_B)
#endif
		   )
		{
			offsetchanged = true;
			cancel = 1;
			return -1;
		}
#ifdef HW_RVL
		if (power)
		{
			PowerOff();
		}
#endif

		if ((p & PAD_BUTTON_A)
#ifdef HW_RVL
		        | (wp & WPAD_BUTTON_A)
#endif
		   )
		{
			return selection;
		}
		if ((p & PAD_BUTTON_DOWN)
#ifdef HW_RVL
		        | (wp & WPAD_BUTTON_DOWN)
#endif
		   )
		{
			upordown = 2;
			selection++;
			if (selection == maxfile)
			{
				selection = offset = 0;
				offsetchanged = true;
			}
			if ((selection - offset) >= PAGESIZE)
			{
				offset += PAGESIZE;
				offsetchanged = true;
			}
			redraw = 1;
		}
		if ((p & PAD_BUTTON_UP)
#ifdef HW_RVL
		        | (wp & WPAD_BUTTON_UP)
#endif
		   )
		{
			upordown = 1;
			selection--;
			if (selection < 0)
			{
				selection = maxfile - 1;
				offset = selection - PAGESIZE + 1;
				offsetchanged = true;
			}
			if (selection < offset)
			{
				offset -= PAGESIZE;
				offsetchanged = true;
			}
			if (offset < 0)
			{
				offset = 0;
			}
			redraw = 1;
		}
	}
	return selection;
}

void writeStatusBar( char *line1, char *line2)
{
	int bgcolor = getcolour(0xff,0xff,0xff);
	DrawBoxFilled(10, 404, 610, 455, bgcolor);
	//setfontcolour(84,174,211);
	setfontcolour(28,28,28);
	DrawText(40, 425, line1);
	DrawText(40, 450, line2);
}

void clearLeftPane()
{
	int bgcolor = getcolour(84,174,211);
	DrawBoxFilled(34, 110, 333, 392, bgcolor);
}

void clearRightPane()
{
	int bgcolor = getcolour(84,174,211);
	DrawBoxFilled(376, 112, 605, 395, bgcolor);
}

void DrawHLine (int x1, int x2, int y, int color)
{
	int i;
	y = 320 * y;
	x1 >>= 1;
	x2 >>= 1;
	for (i = x1; i <= x2; i++)
	{
		u32 *tmpfb = xfb[whichfb];
		tmpfb[y+i] = color;
	}
}

void DrawVLine (int x, int y1, int y2, int color)
{
	int i;
	x >>= 1;
	for (i = y1; i <= y2; i++)
	{
		u32 *tmpfb = xfb[whichfb];
		tmpfb[x + (640 * i) / 2] = color;
	}
}

void DrawBox (int x1, int y1, int x2, int y2, int color)
{
	DrawHLine (x1, x2, y1, color);
	DrawHLine (x1, x2, y2, color);
	DrawVLine (x1, y1, y2, color);
	DrawVLine (x2, y1, y2, color);
}

void DrawBoxFilled (int x1, int y1, int x2, int y2, int color)
{
	int h;
	for (h = y1; h <= y2; h++)
		DrawHLine (x1, x2, h, color);
}










