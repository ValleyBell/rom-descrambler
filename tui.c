#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curses.h>

#include "common.h"

#define A_ATTR			(A_ATTRIBUTES & ~(A_COLOR))
#define COLOR_TITLE		1
#define COLOR_MAINVIEW	2
#define COLOR_MAPPINGS	3
#define COLOR_EDITBOX	4

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

#define VMAP_MODE_DEFAULT	0
#define VMAP_MODE_SELECT	1
#define VMAP_MODE_CHANGE	2


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

static void colorbox(WINDOW *win, chtype color, int hasbox);
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
static UINT8 KeyHandler_NumEntry(NUM_ENTRY_STATE* nes, int key);
static void tui_init(void);
static void tui_deinit(void);
static UINT8 KeyHandler_View(int key);
static void RefreshHexView(void);
void tui_main(void);


static WINDOW* wTitle;
static WINDOW* wView;
static WINDOW* wMaps;

static UINT8 hexShowMode;
static UINT8 activeView;
static VMAP_STATE vmState;

static void colorbox(WINDOW *win, chtype color, int hasbox)
{
	int maxy;
	chtype attr = color & A_ATTR & ~A_REVERSE;
	wattrset(win, COLOR_PAIR(color & A_CHARTEXT) | attr);
	wbkgd(win, COLOR_PAIR(color & A_CHARTEXT) | attr);
	
	werase(win);
	
	maxy = getmaxy(win);
	if (hasbox && (maxy > 2))
		box(win, 0, 0);
	
	touchwin(win);
	wrefresh(win);
	return;
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
	
	mvwprintw(wMaps, 9, 40, "CurLine: %d  ", vmState.curEntry);
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
	case 0x1B:	// ESC
		MappingView_Deactivate();
		activeView = AV_MAIN;
		return 1;
	case 'a':
	case 'A':
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
		vmState.mode = VMAP_MODE_SELECT;
		vmState.selType = DST_DATA;
		mvwprintw(wMaps, 0, 50, "Select Line: %s ", "Data");
		
		getyx(wMaps, vmState.numEntry.textY, vmState.numEntry.textX);
		vmState.numEntry.maxValue = vmState.mapsCnt[vmState.selType] - 1;
		NumEntryState_Init(&vmState.numEntry);
		wclrtoeol(wMaps);
		wrefresh(wMaps);
		return 1;
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
	case '\n':	// Return - change number
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
			
			wmove(wMaps, vmState.numEntry.textY, vmState.numEntry.textX);
			wattron(wMaps, COLOR_PAIR(COLOR_EDITBOX));
			wrefresh(wMaps);
		}
		return 1;
	default:
		break;
	}
	
	return 0;
}

static UINT8 KeyHandler_ChangeValue(int key)
{
	int finishSelect;
	
	finishSelect = KeyHandler_NumEntry(&vmState.numEntry, key);
	if (finishSelect)
	{
		if (finishSelect == 1)
		{
			int mapping, mode;
			
			mapping = MappingView_Entry2Line(vmState.curEntry, &mode);
			DESCRMB_INFO* dsi = GetDescambleInfo(mode);
			if (vmState.numEntry.value < dsi->bitCnt)
			{
				dsi->bitMap[mapping] = vmState.numEntry.value;
				mvwprintw(wMaps, vmState.numEntry.textY, vmState.numEntry.textX, "%2d ", dsi->bitMap[mapping]);
				RefreshHexView();
			}
		}
		
		wattroff(wMaps, COLOR_PAIR(COLOR_EDITBOX));
		vmState.mode = VMAP_MODE_DEFAULT;
		MappingView_RefreshCursorPos(0);
	}
	wrefresh(wMaps);
	return 1;
}

static UINT8 KeyHandler_MappingsSelect(int key)
{
	int finishSelect;
	
	finishSelect = KeyHandler_NumEntry(&vmState.numEntry, key);
	if (finishSelect)
	{
		if (finishSelect == 1)
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

static UINT8 KeyHandler_NumEntry(NUM_ENTRY_STATE* nes, int key)
{
	if (key >= '0' && key <= '9')
	{
		nes->input[nes->inPos] = key;
		mvwaddch(wMaps, nes->textY, nes->textX + nes->inPos, key);
		nes->inPos ++;
		
		nes->value = ParseNumber(nes->input, nes->inPos);
		if (nes->inPos >= nes->maxDigits || nes->value * 10 > nes->maxValue)
		{
			// stop accepting values when
			//	- 1 digit (maxValue 1..10) or 2 digits (mapsCnt 11..100) were entered
			//	- entering another digit would make selValue larger than mapsCnt
			return 1;	// finished parsing
		}
	}
	else if (key == '\n')	// Return key
	{
		if (nes->inPos == 0)
			return 2;	// nothing entered - cancel
		nes->value = ParseNumber(nes->input, nes->inPos);
		return 1;	// finished parsing
	}
	else if (key == 0x1B)	// ESC
	{
		return 2;	// input cancelled
	}
	
	return 0;
}

static void tui_init(void)
{
	initscr();
	start_color();
	init_pair(COLOR_TITLE   , COLOR_BLACK, COLOR_CYAN);
	init_pair(COLOR_MAINVIEW, COLOR_WHITE, COLOR_BLUE);
	init_pair(COLOR_MAPPINGS, COLOR_WHITE, COLOR_CYAN);
	init_pair(COLOR_EDITBOX , COLOR_WHITE, COLOR_BLACK);
	
	wTitle = subwin(stdscr, 1, COLS, 0, 0);
	wView = subwin(stdscr, LINES - MAPSWIN_HEIGHT - 1, COLS, 1, 0);
	wMaps = subwin(stdscr, MAPSWIN_HEIGHT, COLS, LINES - MAPSWIN_HEIGHT, 0);
	
	colorbox(wTitle, COLOR_TITLE, 0);
	colorbox(wView, COLOR_MAINVIEW, 0);
	colorbox(wMaps, COLOR_MAPPINGS, 0);
	wprintw(wTitle, "ROM Descrambler");
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
	HEXVIEW_INFO* hvi = GetHexViewInfo();
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
		hvi->lineChrs ++;
		ResizeHexDisplay();
		needUpdate = 1;
		break;
	case '-':
		if (hvi->lineChrs < 2)
			break;
		hvi->lineChrs --;
		ResizeHexDisplay();
		needUpdate = 1;
		break;
	case 'v':
	case 'V':	// V = view mode
		hexShowMode ++;
		hexShowMode %= 3;
		needUpdate = 1;
		break;
	case 'm':
	case 'M':
		activeView = AV_MAPS;
		MappingView_Activate();
		needUpdate = 1;
		break;
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
	HEXVIEW_INFO* hvi = GetHexViewInfo();
	werase(wView);
	switch(hexShowMode)
	{
	case HSM_EXT:
		wmove(wView, 0, 0);
		ShowHexDump(wView, hvi->startOfs, getmaxy(wView), DSM_NONE);
		break;
	case HSM_INT:
		wmove(wView, 0, 0);
		ShowHexDump(wView, hvi->startOfs, getmaxy(wView), DSM_ADDR | DSM_DATA);
		break;
	case HSM_BOTH:
		wmove(wView, 0, 0);
		ShowHexDump(wView, hvi->startOfs, getmaxy(wView), DSM_NONE);
		wmove(wView, 0, getmaxx(wView) / 2);
		ShowHexDump(wView, hvi->startOfs, getmaxy(wView), DSM_ADDR | DSM_DATA);
		break;
	case HSM_THREE:
		wmove(wView, 0, 0);
		ShowHexDump(wView, hvi->startOfs, getmaxy(wView), 0);
		wmove(wView, 0, getmaxx(wView) / 3);
		ShowHexDump(wView, hvi->startOfs, getmaxy(wView), DSM_ADDR);
		wmove(wView, 0, getmaxx(wView) / 3 * 2);
		ShowHexDump(wView, hvi->startOfs, getmaxy(wView), DSM_ADDR | DSM_DATA);
		break;
	}
	wrefresh(wView);
	return;
}

void tui_main(void)
{
	HEXVIEW_INFO* hvi = GetHexViewInfo();
	hvi->startOfs = 0x00;
	
	activeView = AV_MAIN;
	hexShowMode = HSM_THREE;
	vmState.curEntry = 0;
	vmState.lastColLine = 0;
	
	tui_init();
	DrawMappingsWindow();
	ResizeHexDisplay();
	RefreshHexView();
	while(1)
	{
		int key = getch();
		if (key == ERR)
			continue;	// ERR is returned on timeout
		if (key == 'q' || key == 'Q')
			break;
		if (key == KEY_RESIZE)
		{
			DrawMappingsWindow();
			RefreshHexView();
			continue;
		}
		
		switch(activeView)
		{
		case AV_MAIN:
			KeyHandler_View(key);
			break;
		case AV_MAPS:
			KeyHandler_MappingsMain(key);
			break;
		}
	}
	tui_deinit();
	return;
}