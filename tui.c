#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <curses.h>

#include "common.h"

#define A_ATTR			(A_ATTRIBUTES & ~(A_COLOR))
#define COLOR_TITLE		1
#define COLOR_MAINVIEW	2
#define COLOR_MAPPINGS	3
#define COLOR_EDITBOX	4
#define COLOR_DIALOG	5

#define MAPSWIN_HEIGHT	10
#define MAPS_PER_COL	8

// hex show modes
#define HSM_EXT		0	// external (original, scrambled ROM dump)
#define HSM_INT		1	// internal (descrambled data)
#define HSM_BOTH	2
#define HSM_THREE	3

// active view constants
#define AV_MAIN		0	// main screen (hex view)
#define AV_MAPS		1	// mappings
#define AV_DIALOG	2	// dialog

#define VMAP_MODE_DEFAULT	0
#define VMAP_MODE_SELECT	1
#define VMAP_MODE_CHANGE	2

#define KEY_ESC		0x1B
#define KEY_RETURN	'\n'


typedef struct number_entry_state
{
	int textX;
	int textY;
	int maxValue;
	int maxDigits;
	int value;
	
	char input[8];
	UINT8 inPos;
} NUM_ENTRY_STATE;
typedef struct view_mapping_state
{
	UINT8 mode;	// see VMAP_MODE_*
	int mapsPerCol;
	int mapsCnt[2];
	int mapStartCol[2];
	int mapColCnt[2];
	int colWidth;
	int curEntry;	// ID of the currently active box (one range for ADDR and DAT types)
	int entryCnt;
	int lastColLine;	// line in the column of the previous selection
	
	UINT8 selType;
	NUM_ENTRY_STATE numEntry;
} VMAP_STATE;
#define OEF_BUTTON	0
#define OEF_TOGGLE	1
#define OEF_NUMBER	2
typedef struct option_entry
{
	UINT8 type;	// field type - see OEF_* constants
	char access;	// accessor letter
	int y;	// option Y position
	int cx;	// cursor X position
	int fx;	// field start, X position
	int fw;	// field width
	int maxValue;
	int* valPtr;
} OPT_ENTRY;


static DESCRMB_INFO* GetDescambleInfo(UINT8 type);	// type = DST_*
static void ColorWindow(WINDOW *win, int color, attr_t attr, UINT8 drawBorder);
static UINT8 TextBox_ShowAndEdit(size_t bufLen, char* buffer,
		WINDOW* wParent, int boxY, int boxX, int boxWidth);
static void DrawMappingsWindow(void);
static void MappingView_Activate(void);
static void MappingView_Deactivate(void);
static int MappingView_Line2Entry(int type, int line);
static int MappingView_Entry2Line(int entry, int* type);
static void MappingView_RefreshCursorPos(UINT8 storeLinePos);
static UINT8 KeyHandler_MappingsMain(int key);
static UINT8 KeyHandler_ChangeValue(int key);
static UINT8 KeyHandler_MappingsSelect(int key);
static void NumEntryState_Init(NUM_ENTRY_STATE* nes);
static UINT8 KeyHandler_NumEntry(NUM_ENTRY_STATE* nes, WINDOW* win, int key, UINT8 quickEnd);
static void tui_init(void);
static void tui_deinit(void);
static UINT8 KeyHandler_View(int key);
static void RefreshHexView(void);
static void Dialog_SaveData(void);
static void Dialog_GoToOffset(void);
static UINT8 KeyHandler_Global(int key);
void tui_main(APP_DATA* ad);


static APP_DATA* appData;

static WINDOW* wTitle;
static WINDOW* wView;
static WINDOW* wMaps;

static UINT8 hexShowMode;
static UINT8 activeView;
static VMAP_STATE vmState;
static UINT8 doQuit;

static DESCRMB_INFO* GetDescambleInfo(UINT8 type)
{
	if (type == DST_ADDR)
		return &appData->dsiAddr;
	else if (type == DST_DATA)
		return &appData->dsiData;
	else
		return NULL;
}

static void ColorWindow(WINDOW* win, int color, attr_t attr, UINT8 drawBorder)
{
	wattrset(win, COLOR_PAIR(color) | attr);
	wbkgd(win, COLOR_PAIR(color) | attr);
	werase(win);
	
	if (drawBorder && getmaxy(win) > 2)
		box(win, 0, 0);
	
	touchwin(win);
	return;
}

static void DrawBox(WINDOW* win, int y, int x, int h, int w)
{
	// a box of size (w,h) is drawn to (0,0), (w-1,h-1)
	// For easier handling, subtract the 1 here already.
	w --;	h --;
	mvwaddch(win, y + 0, x + 0, ACS_ULCORNER);
	mvwhline(win, y + 0, x + 1, ACS_HLINE, w - 1);
	mvwaddch(win, y + 0, x + w, ACS_URCORNER);
	mvwvline(win, y + 1, x + 0, ACS_VLINE, h - 1);
	mvwvline(win, y + 1, x + w, ACS_VLINE, h - 1);
	mvwaddch(win, y + h, x + 0, ACS_LLCORNER);
	mvwhline(win, y + h, x + 1, ACS_HLINE, w - 1);
	mvwaddch(win, y + h, x + w, ACS_LRCORNER);
	return;
}

static int DrawCenteredText(WINDOW* win, int y, int x, int w, const char* text)
{
	return mvwaddstr(win, y, x + (w - strlen(text)) / 2, text);
}

static UINT8 TextBox_ShowAndEdit(size_t bufLen, char* buffer,
		WINDOW* wParent, int boxY, int boxX, int boxWidth)
{
	WINDOW* wEdit;
	int wpX, wpY;
	UINT8 fin;
	size_t bufPos;
	size_t bufFill;
	UINT8 insertMode;
	
	getbegyx(wParent, wpY, wpX);
	if (boxWidth < 0)
		boxWidth = getmaxx(wParent) + boxWidth - boxX;
	wEdit = subwin(wParent, 1, boxWidth, wpY + boxY, wpX + boxX);
	
	wattrset(wEdit, COLOR_PAIR(COLOR_EDITBOX));
	wbkgd(wEdit, COLOR_PAIR(COLOR_EDITBOX));
	
	keypad(wEdit, TRUE);
	insertMode = 1;
	curs_set(1);	// show cursor
	
	fin = 0;
	bufLen --;	// exclude \0 terminator
	bufFill = strlen(buffer);
	bufPos = bufFill;
	while(!fin)
	{
		werase(wEdit);
		mvwprintw(wEdit, 0, 0, "%s", buffer);
		wmove(wEdit, 0, bufPos);
		wrefresh(wEdit);
		
		int key = wgetch(wEdit);
		if (key == ERR)
			continue;	// timeout
		switch(key)
		{
		case KEY_ESC:
			fin = 2;	// exit + cancel
			break;
		case KEY_RETURN:
			fin = 1;	// exit + confirm
			break;
		case KEY_LEFT:
			if (bufPos > 0)
				bufPos --;
			break;
		case KEY_RIGHT:
			if (bufPos < bufFill)
				bufPos ++;
			break;
		case KEY_HOME:
			bufPos = 0;
			break;
		case KEY_END:
			bufPos = bufFill;
			break;
		case KEY_DC:	// delete key
			if (bufFill > bufPos)
			{
				memmove(&buffer[bufPos], &buffer[bufPos + 1], bufFill - bufPos);
				bufFill --;
			}
			break;
		case KEY_IC:	// enter insert mode
		case KEY_EIC:	// exit insert mode
			insertMode = !insertMode;
			curs_set(insertMode ? 2 : 1);
			break;
		default:
			if (key == erasechar())	// backspace, ^H
			{
				if (bufPos > 0)
				{
					bufPos --;
					memmove(&buffer[bufPos], &buffer[bufPos + 1], bufFill - bufPos);
					bufFill --;	// decrement later to include the \0 terminator
				}
			}
			else if (key == killchar())	// ^U
			{
				bufFill = 0;
				bufPos = bufFill;
				buffer[bufPos] = '\0';
			}
			else if (isprint(key))
			{
				if (insertMode)
				{
					if (bufFill < bufLen)
					{
						bufFill ++;
						memmove(&buffer[bufPos + 1], &buffer[bufPos], bufFill - bufPos);
						buffer[bufPos] = (char)key;
						bufPos ++;
					}
				}
				else
				{
					if (bufFill < bufLen)
					{
						buffer[bufPos] = (char)key;
						bufPos ++;
						bufFill ++;
						if (bufPos == bufFill)
							buffer[bufPos] = '\0';	// add \0-terminator for safety
					}
				}
			}
		}
	}
	buffer[bufFill] = '\0';	// enforce '\0' terminator at the end
	
	curs_set(0);	// hide cursor
	delwin(wEdit);
	return fin;
}

static void DrawMappingsWindow(void)
{
#define MAPPING_DIR	0
	const char* DIR_STRS[2] = {"external", "internal"};
	const char MODE_CHRS[2] = {'A', 'D'};
	const DESCRMB_INFO* dsi[2];
	int mode;
	int colSt;
	
	werase(wMaps);
	wattron(wMaps, A_BOLD);
	mvwprintw(wMaps, 0, 0, "Line [M]appings");
	wattroff(wMaps, A_BOLD);
	wprintw(wMaps, " (%s ", DIR_STRS[0 ^ MAPPING_DIR]);
	waddch(wMaps, ACS_RARROW);
	wprintw(wMaps, " %s)", DIR_STRS[1 ^ MAPPING_DIR]);
	
	vmState.mapsPerCol = MAPS_PER_COL;
	colSt = 0;
	for (mode = 0; mode < 2; mode ++)
	{
		dsi[mode] = GetDescambleInfo(mode);
		vmState.mapStartCol[mode] = colSt;
		vmState.mapsCnt[mode] = dsi[mode]->bitCnt;
		vmState.mapColCnt[mode] = (vmState.mapsCnt[mode] + vmState.mapsPerCol - 1) / vmState.mapsPerCol;
		colSt += vmState.mapColCnt[mode];
	}
	vmState.colWidth = getmaxx(wMaps) / colSt;
	if (vmState.colWidth > 30)
		vmState.colWidth = 30;
	vmState.entryCnt = 0;
	for (mode = 0; mode < 2; mode ++)
	{
		int line;
		vmState.entryCnt += vmState.mapsCnt[mode];
		for (line = 0; line < vmState.mapsCnt[mode]; line ++)
		{
			int col = line / vmState.mapsPerCol;
			int x = (vmState.mapStartCol[mode] + col) * vmState.colWidth + 2;
			int y = 1 + (line % vmState.mapsPerCol);
			mvwprintw(wMaps, y, x, "%c%2d ", MODE_CHRS[mode], line);
			waddch(wMaps, ACS_RARROW);
			waddch(wMaps, ' ');
			wattron(wMaps, COLOR_PAIR(COLOR_EDITBOX));
			wprintw(wMaps, "%c%2d ", MODE_CHRS[mode], dsi[mode]->bitMap[line]);
			wattroff(wMaps, COLOR_PAIR(COLOR_EDITBOX));
		}
	}
	wrefresh(wMaps);
	return;
}

static void MappingView_Activate(void)
{
	curs_set(1);
	vmState.mode = VMAP_MODE_DEFAULT;
	MappingView_RefreshCursorPos(0);
	wrefresh(wMaps);
	return;
}

static void MappingView_Deactivate(void)
{
	curs_set(0);
	return;
}

static int MappingView_Line2Entry(int type, int line)
{
	int curType;
	int entry;
	
	entry = 0;
	for (curType = 0; curType < type; curType ++)
		entry += vmState.mapsCnt[curType];
	return entry + line;
}

static int MappingView_Entry2Line(int entry, int* type)
{
	int curType;
	int line;
	
	line = vmState.curEntry;
	for (curType = 0; curType < 2; curType ++)
	{
		if (line < vmState.mapsCnt[curType])
			break;
		line -= vmState.mapsCnt[curType];
	}
	if (curType >= 2)
		return -1;
	if (type != NULL)
		*type = curType;
	return line;
}

static void MappingView_RefreshCursorPos(UINT8 storeLinePos)
{
	int mode;
	int mapping;
	int col;
	int line;
	int x, y;
	
	mapping = MappingView_Entry2Line(vmState.curEntry, &mode);
	if (mapping < 0)
		return;
	col = mapping / vmState.mapsPerCol;
	line = mapping % vmState.mapsPerCol;
	if (storeLinePos)
		vmState.lastColLine = line;
	x = (vmState.mapStartCol[mode] + col) * vmState.colWidth + 2;
	y = 1 + line;
	wmove(wMaps, y, x + 6);
	return;
}

static int ParseNumber(const char* buffer, size_t len)
{
	int value = 0;
	for (; len > 0; len --, buffer ++)
	{
		if (*buffer < '0' || *buffer > '9')
			break;
		value *= 10;
		value += (*buffer - '0');
	}
	return value;
}

static UINT8 KeyHandler_MappingsMain(int key)
{
	UINT8 didProc = 0;
	if (vmState.mode == VMAP_MODE_SELECT)
		didProc = KeyHandler_MappingsSelect(key);
	else if (vmState.mode == VMAP_MODE_CHANGE)
		didProc = KeyHandler_ChangeValue(key);
	if (didProc)
		return didProc;

	switch(key)
	{
	case KEY_ESC:
		MappingView_Deactivate();
		activeView = AV_MAIN;
		return 1;
	case 'a':
	case 'A':
		if (vmState.mode == VMAP_MODE_CHANGE)
			KeyHandler_ChangeValue(KEY_ESC);
		vmState.mode = VMAP_MODE_SELECT;
		vmState.selType = DST_ADDR;
		mvwprintw(wMaps, 0, 50, "Select Line: %s ", "Address");
		
		getyx(wMaps, vmState.numEntry.textY, vmState.numEntry.textX);
		vmState.numEntry.maxValue = vmState.mapsCnt[vmState.selType] - 1;
		NumEntryState_Init(&vmState.numEntry);
		wclrtoeol(wMaps);
		wrefresh(wMaps);
		return 1;
	case 'd':
	case 'D':
		if (vmState.mode == VMAP_MODE_CHANGE)
			KeyHandler_ChangeValue(KEY_ESC);
		vmState.mode = VMAP_MODE_SELECT;
		vmState.selType = DST_DATA;
		mvwprintw(wMaps, 0, 50, "Select Line: %s ", "Data");
		
		getyx(wMaps, vmState.numEntry.textY, vmState.numEntry.textX);
		vmState.numEntry.maxValue = vmState.mapsCnt[vmState.selType] - 1;
		NumEntryState_Init(&vmState.numEntry);
		wclrtoeol(wMaps);
		wrefresh(wMaps);
		return 1;
	case 'r':
	case 'R':	// revert all mappings
		if (vmState.mode == VMAP_MODE_CHANGE)
			KeyHandler_ChangeValue(KEY_ESC);
		{
			int mode;
			for (mode = 0; mode < 2; mode ++)
			{
				DESCRMB_INFO* dsi = GetDescambleInfo(mode);
				DSI_Invert(dsi);
			}
			RefreshHexView();
			DrawMappingsWindow();
			MappingView_RefreshCursorPos(0);
			wrefresh(wMaps);
		}
		break;
	case KEY_UP:
		if (vmState.curEntry > 0)
			vmState.curEntry --;
		MappingView_RefreshCursorPos(1);
		wrefresh(wMaps);
		break;
	case KEY_DOWN:
		if (vmState.curEntry < vmState.entryCnt - 1)
			vmState.curEntry ++;
		MappingView_RefreshCursorPos(1);
		wrefresh(wMaps);
		break;
	case KEY_LEFT:
		{
			int mapping, mode, col;
			int newEntry;
			mapping = MappingView_Entry2Line(vmState.curEntry, &mode);
			if (mapping < 0)
				break;
			col = mapping / vmState.mapsPerCol - 1;	// previous column
			if (col < 0)
			{
				if (mode == 0)
					break;
				mode --;
				col = vmState.mapColCnt[mode] - 1;
			}
			newEntry = col * vmState.mapsPerCol + vmState.lastColLine;
			if (newEntry >= vmState.mapsCnt[mode])
				newEntry = vmState.mapsCnt[mode] - 1;	// clamping for not-fully-filled columns
			vmState.curEntry = MappingView_Line2Entry(mode, newEntry);
			MappingView_RefreshCursorPos(0);
			wrefresh(wMaps);
		}
		return 1;
	case KEY_RIGHT:
		{
			int mapping, mode, col;
			int newEntry;
			mapping = MappingView_Entry2Line(vmState.curEntry, &mode);
			if (mapping < 0)
				break;
			col = mapping / vmState.mapsPerCol + 1;	// next column
			if (col >= vmState.mapColCnt[mode])
			{
				if (mode >= 1)
					break;
				mode ++;
				col = 0;
			}
			newEntry = col * vmState.mapsPerCol + vmState.lastColLine;
			if (newEntry >= vmState.mapsCnt[mode])
				newEntry = vmState.mapsCnt[mode] - 1;
			vmState.curEntry = MappingView_Line2Entry(mode, newEntry);
			MappingView_RefreshCursorPos(0);
			wrefresh(wMaps);
		}
		return 1;
	case '+':
	case '-':
		if (vmState.mode == VMAP_MODE_CHANGE)
			KeyHandler_ChangeValue(KEY_ESC);
		{
			DESCRMB_INFO* dsi;
			int mapping, mode;
			int textX, textY;
			
			mapping = MappingView_Entry2Line(vmState.curEntry, &mode);
			dsi = GetDescambleInfo(mode);
			
			MappingView_RefreshCursorPos(0);
			getyx(wMaps, textY, textX);
			textX ++;	// the default cursor position is the letter
			if (dsi == NULL)
				return 1;
			if (key == '-')
			{
				if (dsi->bitMap[mapping] == 0)
					return 1;
				dsi->bitMap[mapping] --;
			}
			else if (key == '+')
			{
				if (dsi->bitMap[mapping] >= dsi->bitCnt - 1)
					return 1;
				dsi->bitMap[mapping] ++;
			}
			RefreshHexView();
			
			wattron(wMaps, COLOR_PAIR(COLOR_EDITBOX));
			mvwprintw(wMaps, textY, textX, "%2d ", dsi->bitMap[mapping]);
			wattroff(wMaps, COLOR_PAIR(COLOR_EDITBOX));
			MappingView_RefreshCursorPos(0);
			wrefresh(wMaps);
		}
		return 1;
	default:
		if (key >= '0' && key <= '9')
		{
			DESCRMB_INFO* dsi;
			int mode;
			
			MappingView_Entry2Line(vmState.curEntry, &mode);
			vmState.selType = mode;
			dsi = GetDescambleInfo(vmState.selType);
			
			vmState.mode = VMAP_MODE_CHANGE;
			
			MappingView_RefreshCursorPos(0);
			getyx(wMaps, vmState.numEntry.textY, vmState.numEntry.textX);
			vmState.numEntry.textX ++;	// the default cursor position is the letter
			vmState.numEntry.maxValue = dsi->bitCnt - 1;
			NumEntryState_Init(&vmState.numEntry);
			
			wattron(wMaps, COLOR_PAIR(COLOR_EDITBOX));
			mvwhline(wMaps, vmState.numEntry.textY, vmState.numEntry.textX, ' ', 3);	// erase text
			wmove(wMaps, vmState.numEntry.textY, vmState.numEntry.textX);
			KeyHandler_ChangeValue(key);	// process the number
			return 1;
		}
		break;
	}
	
	return 0;
}

static UINT8 KeyHandler_ChangeValue(int key)
{
	UINT8 finishSelect = KeyHandler_NumEntry(&vmState.numEntry, wMaps, key, 0);
	if (finishSelect & 0x80)
	{
		int mapping, mode;
		
		mapping = MappingView_Entry2Line(vmState.curEntry, &mode);
		DESCRMB_INFO* dsi = GetDescambleInfo(mode);
		// finishSelect == 0x82 -> keep previous value
		if (finishSelect == 0x81 || finishSelect == 0xFF)	// "confirm" (0x81) or "move" (0xFF)
		{
			if (vmState.numEntry.value < dsi->bitCnt)	// apply value when in range
			{
				dsi->bitMap[mapping] = vmState.numEntry.value;
				RefreshHexView();
			}
		}
		mvwprintw(wMaps, vmState.numEntry.textY, vmState.numEntry.textX, "%2d ", dsi->bitMap[mapping]);
		
		wattroff(wMaps, COLOR_PAIR(COLOR_EDITBOX));
		vmState.mode = VMAP_MODE_DEFAULT;
		MappingView_RefreshCursorPos(0);
	}
	wrefresh(wMaps);
	if (finishSelect == 0x00 || finishSelect == 0xFF)
		return 0;	// forward key to parent
	return 1;
}

static UINT8 KeyHandler_MappingsSelect(int key)
{
	UINT8 finishSelect = KeyHandler_NumEntry(&vmState.numEntry, wMaps, key, 1);
	if (finishSelect & 0x80)
	{
		// movement key (select==0xFF) -> cancel
		if (finishSelect == 0x81)
		{
			if (vmState.numEntry.value < vmState.mapsCnt[vmState.selType])
				vmState.curEntry = MappingView_Line2Entry(vmState.selType, vmState.numEntry.value);
		}
		
		wmove(wMaps, 0, 50);
		wclrtoeol(wMaps);
		vmState.mode = VMAP_MODE_DEFAULT;
		MappingView_RefreshCursorPos(1);
	}
	wrefresh(wMaps);
	if (finishSelect == 0x00 || finishSelect == 0xFF)
		return 0;	// forward key to parent
	return 1;
}

static void NumEntryState_Init(NUM_ENTRY_STATE* nes)
{
	int value;
	
	nes->inPos = 0;
	nes->maxDigits = 0;
	value = nes->maxValue;
	do
	{
		nes->maxDigits ++;
		value /= 10;
	} while(value > 0);
	
	return;
}

static UINT8 KeyHandler_NumEntry(NUM_ENTRY_STATE* nes, WINDOW* win, int key, UINT8 quickEnd)
{
	switch(key)
	{
	case KEY_RETURN:
		if (nes->inPos == 0)
			return 0x82;	// nothing entered - cancel
		nes->value = ParseNumber(nes->input, nes->inPos);
		return 0x81;	// finished parsing
	case KEY_ESC:
		return 0x82;	// input cancelled
	case KEY_UP:
	case KEY_DOWN:
	case KEY_LEFT:
	case KEY_RIGHT:
		return 0xFF;
	}
	
	if (key >= '0' && key <= '9')
	{
		if (nes->inPos >= nes->maxDigits)
		{
			// when entering more digits than allowed, discard the first (oldest) digit
			memmove(&nes->input[0], &nes->input[1], nes->inPos - 1);
			nes->inPos --;
			mvwprintw(win, nes->textY, nes->textX, "%*s", nes->inPos, nes->input);
		}
		nes->input[nes->inPos] = key;
		mvwaddch(win, nes->textY, nes->textX + nes->inPos, key);
		nes->inPos ++;
		
		nes->value = ParseNumber(nes->input, nes->inPos);
		if (quickEnd)
		{
			if (nes->inPos >= nes->maxDigits || nes->value * 10 > nes->maxValue)
			{
				// stop accepting values when
				//	- 1 digit (maxValue 1..10) or 2 digits (mapsCnt 11..100) were entered
				//	- entering another digit would make selValue larger than mapsCnt
				return 0x81;	// finished parsing
			}
		}
		return 0x01;	// did process, but not finished
	}
	
	return 0x00;	// not processed
}

static void tui_init(void)
{
	initscr();
	start_color();
	init_pair(COLOR_TITLE   , COLOR_BLACK, COLOR_CYAN);
	init_pair(COLOR_MAINVIEW, COLOR_WHITE, COLOR_BLUE);
	init_pair(COLOR_MAPPINGS, COLOR_WHITE, COLOR_CYAN);
	init_pair(COLOR_EDITBOX , COLOR_WHITE, COLOR_BLACK);
	init_pair(COLOR_DIALOG  , COLOR_BLACK, COLOR_CYAN);
	
	wTitle = subwin(stdscr, 1, COLS, 0, 0);
	wView = subwin(stdscr, LINES - MAPSWIN_HEIGHT - 1, COLS, 1, 0);
	wMaps = subwin(stdscr, MAPSWIN_HEIGHT, COLS, LINES - MAPSWIN_HEIGHT, 0);
	
	ColorWindow(wTitle, COLOR_TITLE, A_NORMAL, 0);
	ColorWindow(wView, COLOR_MAINVIEW, A_NORMAL, 0);
	ColorWindow(wMaps, COLOR_MAPPINGS, A_NORMAL, 0);
	wprintw(wTitle, "ROM Descrambler - %s", appData->filePath);
	wrefresh(wTitle);
	
	cbreak();
	keypad(stdscr, TRUE);
	noecho();
	curs_set(0);
	nodelay(stdscr, FALSE);
	halfdelay(500);
	scrollok(wView, TRUE); // enable scrolling in main window
	
	leaveok(stdscr, TRUE);
	leaveok(wTitle, TRUE);
	leaveok(wView, TRUE);
	leaveok(wMaps, FALSE);	// we show the cursor in this window
	return;
}

static void tui_deinit(void)
{
	delwin(wTitle);	wTitle = NULL;
	delwin(wView);	wView = NULL;
	delwin(wMaps);	wMaps = NULL;
	curs_set(1);
	endwin();
	return;
}

static UINT8 KeyHandler_View(int key)
{
	HEXVIEW_INFO* hvi = &appData->info;
	size_t lastLineOfs = (hvi->endOfs >= hvi->lineChrs) ? (hvi->endOfs - hvi->lineChrs) : 0;
	int hexOfsMove = 0;
	UINT8 needUpdate = 0;
	
	switch(key)
	{
	case KEY_UP:
		hexOfsMove = -hvi->lineChrs;
		break;
	case KEY_DOWN:
		hexOfsMove = +hvi->lineChrs;
		break;
	case KEY_LEFT:
		hexOfsMove = -1;
		break;
	case KEY_RIGHT:
		hexOfsMove = +1;
		break;
	case KEY_PPAGE:
		hexOfsMove = -hvi->lineChrs * getmaxy(wView);
		break;
	case KEY_NPAGE:
		hexOfsMove = +hvi->lineChrs * getmaxy(wView);
		break;
	case KEY_HOME:
		hvi->startOfs = 0x00;
		needUpdate = 1;
		break;
	case KEY_END:
		hvi->startOfs = lastLineOfs;
		needUpdate = 1;
		break;
	case '+':
		{
			size_t maxChrs;
			size_t scrWidth = (size_t)getmaxx(wView);
			if (hexShowMode == HSM_BOTH)
				scrWidth /= 2;
			else if (hexShowMode == HSM_THREE)
				scrWidth /= 3;
			maxChrs = GetMaxBytesPerLine(&appData->info, &appData->work, scrWidth);
			if (hvi->lineChrs >= maxChrs)
				break;
		}
		hvi->lineChrs ++;
		ResizeHexDisplay(&appData->info, &appData->work);
		needUpdate = 1;
		break;
	case '-':
		if (hvi->lineChrs < 2)
			break;
		hvi->lineChrs --;
		ResizeHexDisplay(&appData->info, &appData->work);
		needUpdate = 1;
		break;
	case 'v':
	case 'V':	// V = view mode
		hexShowMode ++;
		hexShowMode %= 4;
		needUpdate = 1;
		{
			size_t maxChrs;
			size_t scrWidth = (size_t)getmaxx(wView);
			if (hexShowMode == HSM_BOTH)
				scrWidth /= 2;
			else if (hexShowMode == HSM_THREE)
				scrWidth /= 3;
			maxChrs = GetMaxBytesPerLine(&appData->info, &appData->work, scrWidth);
			if (hvi->lineChrs > maxChrs)
			{
				hvi->lineChrs = maxChrs;
				ResizeHexDisplay(&appData->info, &appData->work);
			}
		}
		break;
	case 'm':
	case 'M':	// M = focus Mappings view
		activeView = AV_MAPS;
		MappingView_Activate();
		needUpdate = 1;
		break;
	case 'g':
	case 'G':	// G = go to offset
		Dialog_GoToOffset();
		return 1;
	}
	if (hexOfsMove < 0)
	{
		if (hvi->startOfs > -hexOfsMove)
			hvi->startOfs += hexOfsMove;
		else
			hvi->startOfs = 0x00;
		needUpdate = 1;
	}
	else if (hexOfsMove > 0)
	{
		hvi->startOfs += hexOfsMove;
		if (hvi->startOfs >= hvi->endOfs)
		{
			size_t lineOfs = hvi->startOfs % hvi->lineChrs;
			hvi->startOfs = lastLineOfs + lineOfs;
		}
		needUpdate = 1;
	}
	if (needUpdate)
	{
		RefreshHexView();
		return 1;
	}
	return 0;
}

static void RefreshHexView(void)
{
	HEXVIEW_INFO* hvi = &appData->info;
	werase(wView);
	switch(hexShowMode)
	{
	case HSM_EXT:
		wmove(wView, 0, 0);
		ShowHexDump(appData, wView, hvi->startOfs, getmaxy(wView), DSM_NONE);
		break;
	case HSM_INT:
		wmove(wView, 0, 0);
		ShowHexDump(appData, wView, hvi->startOfs, getmaxy(wView), DSM_ADDR | DSM_DATA);
		break;
	case HSM_BOTH:
		wmove(wView, 0, 0);
		ShowHexDump(appData, wView, hvi->startOfs, getmaxy(wView), DSM_NONE);
		wmove(wView, 0, getmaxx(wView) / 2);
		ShowHexDump(appData, wView, hvi->startOfs, getmaxy(wView), DSM_ADDR | DSM_DATA);
		break;
	case HSM_THREE:
		wmove(wView, 0, 0);
		ShowHexDump(appData, wView, hvi->startOfs, getmaxy(wView), 0);
		wmove(wView, 0, getmaxx(wView) / 3);
		ShowHexDump(appData, wView, hvi->startOfs, getmaxy(wView), DSM_ADDR);
		wmove(wView, 0, getmaxx(wView) / 3 * 2);
		ShowHexDump(appData, wView, hvi->startOfs, getmaxy(wView), DSM_ADDR | DSM_DATA);
		break;
	}
	wrefresh(wView);
	return;
}

static void Dialog_SaveData(void)
{
	WINDOW* wDlg;
	int oldx, oldy, maxx, maxy;
	const int dlgHeight = 3;
	const int dlgWidth = 60;
	int dlgX = (getmaxx(stdscr) - dlgWidth) / 2;
	int dlgY = (getmaxy(stdscr) - dlgHeight) / 2;
	UINT8 fin;
	char fileName[0x200];
	
	wDlg = newwin(dlgHeight, dlgWidth, dlgY, dlgX);
	ColorWindow(wDlg, COLOR_DIALOG, A_NORMAL, 1);
	
	DrawCenteredText(wDlg, 0, 0, dlgWidth, "Save descrambled data");
	mvwaddstr(wDlg, 1, 2, "File Name: ");
	wrefresh(wDlg);
	strcpy(fileName, "out.bin");
	fin = TextBox_ShowAndEdit(sizeof(fileName), fileName, wDlg, 1, getcurx(wDlg), -2);
	
	if (fin == 1)
	{
		int posY = dlgHeight - 1;
		UINT8 retVal;
		
		mvwaddstr(wDlg, posY, 1, "Saving ...");
		wrefresh(wDlg);
		retVal = SaveDescrambledFile(fileName, appData);
		if (!retVal)
			mvwaddstr(wDlg, posY, 1, "Done.     ");
		else
			mvwaddstr(wDlg, posY, 1, "Failed.   ");
		wrefresh(wDlg);
		wgetch(wDlg);
	}
	delwin(wDlg);
	touchwin(stdscr);
	wrefresh(stdscr);
	return;
}

static void Dialog_GoToOffset(void)
{
	WINDOW* wDlg;
	int oldx, oldy, maxx, maxy;
	const int dlgHeight = 3;
	const int dlgWidth = 32;
	int dlgX = (getmaxx(stdscr) - dlgWidth) / 2;
	int dlgY = (getmaxy(stdscr) - dlgHeight) / 2;
	UINT8 fin;
	char ofsStr[0x09];	// enough for 32-bit offsets
	
	wDlg = newwin(dlgHeight, dlgWidth, dlgY, dlgX);
	ColorWindow(wDlg, COLOR_DIALOG, A_NORMAL, 1);
	
	DrawCenteredText(wDlg, 0, 0, dlgWidth, "Go To Offset");
	mvwaddstr(wDlg, 1, 2, "Offset: 0x");
	wrefresh(wDlg);
	strcpy(ofsStr, "");
	fin = TextBox_ShowAndEdit(sizeof(ofsStr), ofsStr, wDlg, 1, getcurx(wDlg), -2);
	
	if (fin == 1)
	{
		char* endPtr;
		size_t newOfs = (size_t)strtoul(ofsStr, &endPtr, 0x10);
		if (endPtr != ofsStr)
		{
			if (newOfs < appData->info.endOfs)
				appData->info.startOfs = newOfs;
			RefreshHexView();
		}
	}
	delwin(wDlg);
	touchwin(stdscr);
	wrefresh(stdscr);
	return;
}

static void PrintOptEntry(OPT_ENTRY* oe, WINDOW* win, UINT8 type, int y, int x, int cx, int cw, const char* text, char letter)
{
	const char* lPos = strchr(text, letter);
	
	oe->type = type;
	oe->access = (char)toupper((unsigned char)letter);
	oe->y = y;
	oe->fx = cx;
	oe->fw = cw;
	oe->cx = (cw == 0) ? cx : (cx + cw - 1);
	
	mvwaddstr(win, y, x, text);
	if (lPos != 0)
	{
		wmove(win, y, x + (lPos - text));
		wchgat(win, 1, A_NORMAL, COLOR_MAPPINGS, NULL);
	}
	return;
}

static void Dialog_Options(void)
{
	struct {
		int aCount;
		int dCount;
		int bytesRow;
		int groupSize;
		UINT8 cfgAutoSave;
	} optState;
	OPT_ENTRY optEnt[8];
	WINDOW* wDlg;
	const int dlgHeight = 14;
	const int dlgWidth = 32;
	int entryMax;
	int dlgX = (getmaxx(stdscr) - dlgWidth) / 2;
	int dlgY = (getmaxy(stdscr) - dlgHeight) / 2;
	int curEntry;
	OPT_ENTRY* curOE;
	UINT8 doQuit;
	UINT8 didChange;
	int cursorPosChg;
	NUM_ENTRY_STATE nes;
	
	optState.aCount = (int)appData->dsiAddr.bitCnt;
	optState.dCount = (int)appData->dsiData.bitCnt;
	optState.bytesRow = (int)appData->info.lineChrs;
	optState.groupSize = (int)appData->info.wordSize;
	optState.cfgAutoSave = appData->cfgAutoSave;
	
	wDlg = newwin(dlgHeight, dlgWidth, dlgY, dlgX);
	ColorWindow(wDlg, COLOR_DIALOG, A_NORMAL, 1);
	
	DrawCenteredText(wDlg, 0, 0, dlgWidth, "Options");
	curEntry = 2;
	
	dlgY = dlgHeight - 2;
	dlgX = dlgWidth / 2;
	PrintOptEntry(&optEnt[0], wDlg, OEF_BUTTON, dlgY, dlgX - 8, dlgX - 6, 0, "[ OK ]", 'O');
	PrintOptEntry(&optEnt[1], wDlg, OEF_BUTTON, dlgY, dlgX + 0, dlgX + 2, 0, "[ Cancel ]", 'C');
	
	DrawBox  (wDlg, 1, 2, 4, dlgWidth - 4);
	mvwaddstr(wDlg, 1, 4, "Mappings");
	PrintOptEntry(&optEnt[2], wDlg, OEF_NUMBER, 2, 4, 24, 4, "Address entrys:", 'A');
	optEnt[2].maxValue = 32;
	optEnt[2].valPtr = &optState.aCount;
	PrintOptEntry(&optEnt[3], wDlg, OEF_NUMBER, 3, 4, 24, 4, "Data entrys:", 'D');
	optEnt[3].maxValue = 8;
	optEnt[3].valPtr = &optState.dCount;
	
	DrawBox  (wDlg, 5, 2, 4, dlgWidth - 4);
	mvwaddstr(wDlg, 5, 4, "Hex View");
	PrintOptEntry(&optEnt[4], wDlg, OEF_NUMBER, 6, 4, 24, 4, "Bytes per row:", 'B');
	optEnt[4].maxValue = 999;
	optEnt[4].valPtr = &optState.bytesRow;
	PrintOptEntry(&optEnt[5], wDlg, OEF_NUMBER, 7, 4, 24, 4, "Group size:", 'G');
	optEnt[5].maxValue = 999;
	optEnt[5].valPtr = &optState.groupSize;
	
	DrawBox  (wDlg, 9, 2, 3, dlgWidth - 4);
	mvwaddstr(wDlg, 9, 4, "Miscellaneous");
	PrintOptEntry(&optEnt[6], wDlg, OEF_TOGGLE, 10, 4, 25, 0, "Config auto-save:", 'v');
	mvwprintw(wDlg,10, 24, "[ ]");
	entryMax = 7;
	
	wattron(wDlg, COLOR_PAIR(COLOR_EDITBOX));
	for (curEntry = 2; curEntry <= 5; curEntry ++)
		mvwprintw(wDlg, optEnt[curEntry].y, optEnt[curEntry].fx, "%*d", optEnt[curEntry].fw, *optEnt[curEntry].valPtr);
	wattroff(wDlg, COLOR_PAIR(COLOR_EDITBOX));
	mvwaddch(wDlg, optEnt[6].y, optEnt[6].fx, optState.cfgAutoSave ? 'x' : ' ');
	wrefresh(wDlg);
	
	keypad(wDlg, TRUE);
	curs_set(1);
	curEntry = 2;	curOE = &optEnt[curEntry];
	wmove(wDlg, curOE->y, curOE->cx);
	doQuit = 0;
	didChange = 0;
	while(!doQuit)
	{
		int key = wgetch(wDlg);
		UINT8 finishSelect = 0x00;
		
		if (curOE->type == OEF_NUMBER && didChange)
		{
			finishSelect = KeyHandler_NumEntry(&nes, wDlg, key, 0);
			if (! (finishSelect == 0x00 || finishSelect == 0xFF))
				key = 0;
		}
		else if (curOE->type == OEF_TOGGLE)
		{
			if (key == ' ')
			{
				if (curEntry == 6)
				{
					optState.cfgAutoSave = !optState.cfgAutoSave;
					mvwaddch(wDlg, curOE->y, curOE->fx, optState.cfgAutoSave ? 'x' : ' ');
					wmove(wDlg, curOE->y, curOE->cx);
				}
				key = 0;
			}
		}
		else if (curOE->type == OEF_BUTTON)
		{
			if (key == KEY_RETURN || key == ' ')
			{
				if (curEntry == 0)	// OK
					doQuit = 1;
				else if (curEntry == 1)	// Cancel
					doQuit = 2;
				key = 0;
			}
		}
		
		cursorPosChg = 0;
		switch(key)
		{
		case KEY_ESC:
			doQuit = 2;
			break;
		case KEY_RETURN:
			doQuit = 1;
			break;
		case KEY_UP:
			cursorPosChg = -1;
			break;
		case KEY_DOWN:
			cursorPosChg = +1;
			break;
		case '+':
		case '-':
			if (curOE->type == OEF_NUMBER)
			{
				int newVal;
				if (!didChange)
					KeyHandler_NumEntry(&nes, wDlg, KEY_ESC, 0);
				if (key == '+')
					newVal = *curOE->valPtr + 1;
				else //if (key == '-')
					newVal = *curOE->valPtr - 1;
				if (newVal >= 0 && newVal <= curOE->maxValue)
					*curOE->valPtr = newVal;
				wattron(wDlg, COLOR_PAIR(COLOR_EDITBOX));
				mvwprintw(wDlg, curOE->y, curOE->fx, "%*d", curOE->fw, *curOE->valPtr);
				wattroff(wDlg, COLOR_PAIR(COLOR_EDITBOX));
				wmove(wDlg, curOE->y, curOE->cx);
				didChange = 0;
				finishSelect = 0;
			}
			break;
		default:
			if (key >= '0' && key <= '9')
			{
				if (curOE->type == OEF_NUMBER && !didChange)
				{
					wattron(wDlg, COLOR_PAIR(COLOR_EDITBOX));
					nes.textX = curOE->fx;
					nes.textY = curOE->y;
					nes.maxValue = curOE->maxValue;
					nes.value = *curOE->valPtr;
					NumEntryState_Init(&nes);
					nes.textX = curOE->fx + curOE->fw - nes.maxDigits;	// less strict left-alignment
					mvwhline(wDlg, curOE->y, curOE->fx, ' ', curOE->fw);	// erase text
					finishSelect = KeyHandler_NumEntry(&nes, wDlg, key, 0);
					didChange = 1;
				}
			}
			else
			{
				// handle accessor keys
				int keyOE;
				char keyChr = '\0';
				if (isalpha(key))
					keyChr = (char)toupper(key);
				for (keyOE = 0; keyOE < entryMax; keyOE ++)
				{
					if (optEnt[keyOE].access == keyChr)
					{
						cursorPosChg = keyOE - curEntry;
						if (didChange)
							finishSelect = 0xFF;
						break;
					}
				}
			}
			break;
		}
		if (curOE->type == OEF_NUMBER && (finishSelect & 0x80))
		{
			// finishSelect == 0x82 -> keep previous value
			if (finishSelect == 0x81 || finishSelect == 0xFF)	// "confirm" (0x81) or "move" (0xFF)
			{
				if (nes.value <= curOE->maxValue)
					*curOE->valPtr = nes.value;
			}
			mvwprintw(wDlg, curOE->y, curOE->fx, "%*d", curOE->fw, *curOE->valPtr);
			wmove(wDlg, curOE->y, curOE->cx);
			didChange = 0;
		}
		if (cursorPosChg != 0)
		{
			curEntry += cursorPosChg;
			if (curEntry < 0)
				curEntry += entryMax;
			else if (curEntry >= entryMax)
				curEntry -= entryMax;
			curOE = &optEnt[curEntry];
			wmove(wDlg, curOE->y, curOE->cx);
			wattroff(wDlg, COLOR_PAIR(COLOR_EDITBOX));
			didChange = 0;
		}
	}
	
	curs_set(0);
	delwin(wDlg);
	if (doQuit == 1)
	{
		DSI_Resize(&appData->dsiAddr, (UINT8)optState.aCount);
		DSI_Resize(&appData->dsiData, (UINT8)optState.dCount);
		appData->info.lineChrs = (size_t)optState.bytesRow;
		appData->info.wordSize = (size_t)optState.groupSize;
		ResizeHexDisplay(&appData->info, &appData->work);
		appData->cfgAutoSave = optState.cfgAutoSave;
		
		DrawMappingsWindow();
		RefreshHexView();
	}
	touchwin(stdscr);
	wrefresh(stdscr);
	return;
}

static UINT8 KeyHandler_Global(int key)
{
	switch(key)
	{
	case 'q':
	case 'Q':	// Q = quit
		doQuit = 1;
		return 1;
	case 'c':
	case 'C':	// C = configuration load/save
		//activeView = AV_DIALOG;
		return 1;
	case 's':
	case 'S':	// S = save descrambled data
		Dialog_SaveData();
		return 1;
	case 'o':
	case 'O':	// O = options dialog
		Dialog_Options();
		return 1;
	case KEY_F(1):
		// display help dialog
		return 1;
	}
	return 0;
}

void tui_main(APP_DATA* ad)
{
	UINT8 didProc;
	
	appData = ad;
	appData->info.startOfs = 0x00;
	
	activeView = AV_MAIN;
	hexShowMode = HSM_BOTH;
	vmState.curEntry = 0;
	vmState.lastColLine = 0;
	
	tui_init();
	doQuit = 0;
	
	DrawMappingsWindow();
	ResizeHexDisplay(&appData->info, &appData->work);
	RefreshHexView();
	while(!doQuit)
	{
		int key = getch();
		if (key == ERR)
			continue;	// ERR is returned on timeout
		if (key == KEY_RESIZE)
		{
			DrawMappingsWindow();
			RefreshHexView();
			continue;
		}
		
		switch(activeView)
		{
		case AV_MAIN:
			didProc = KeyHandler_View(key);
			break;
		case AV_MAPS:
			didProc = KeyHandler_MappingsMain(key);
			break;
		default:
			didProc = 0;
			break;
		}
		if (! didProc)
			didProc = KeyHandler_Global(key);
	}
	tui_deinit();
	return;
}
