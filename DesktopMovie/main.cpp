// �Q�l�ɂ����L��
// https://qiita.com/Raitichan82/items/eee6ef7b5a37cf469740
// https://www.natsuneko.blog/entry/2016/12/31/wallpaper-engine-concept
// https://www.codeproject.com/Articles/856020/Draw-Behind-Desktop-Icons-in-Windows-plus
// https://qiita.com/takamon9/items/603a2956b640904e2043
// https://nodamushi.hatenablog.com/entry/2018/09/17/024153
// http://yamatyuu.net/computer/program/sdk/base/enumdisplay/index.html
// http://www.orangemaker.sakura.ne.jp/labo/memo/sdk-mfc/win7Desktop.html

// �g���C�֘A
#define TRAY_WINAPI 1
#include "tray.h"

#pragma comment(lib, "SHCore.lib")   // ���ۂ̉𑜓x���擾���邽�߂ɕK�v
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "opencv_world451.lib")

#include <Windows.h>
#include <opencv2/highgui/highgui_c.h>
#include <opencv2/opencv.hpp>
#include <shellscalingapi.h>
#include <string>
#include <thread>
#include <chrono>
#include <fstream>
#include <iostream>

// �����init�t�@�C������ǂݍ��ތ`���ɕς���
#define ICON_FILENAME "icon.png"		// �g���C�A�C�R���̃t�@�C����
#define LOG_FILENAME "log.txt"			// ���O�t�@�C���̖��O
#define CONFIG_FILENAME "config.csv"	// �ݒ�t�@�C���̖��O


// global�ϐ�
volatile bool is_process_done = false;				// �v���Z�X���s��or�I���̃t���O
volatile bool is_update_wallpaper = false;			// �ǎ����X�V���邩�ǂ����B�͂��߂͍X�V���Ȃ�
volatile int max_camera_num = 5;       				// PC�ɐڑ�����Ă���J�����̐�
volatile int camera_num = 0;						// �g�p����J�����̔ԍ�
volatile float fps = 30;							// fps
std::string movie_filename = "movie.mp4";			// ����̃t�@�C����


													/* **************************************************************** */
/* ********************* �ݒ�t�@�C���̃��[�h ********************* */
/* **************************************************************** */
bool LoadConfigFile(volatile int* __max_camera_num, std::string* __movie_filename) {
	std::ifstream ifs(CONFIG_FILENAME);
	std::string tmp;
	std::string cmd;
	std::string value;
	if (ifs.fail()) return false;

	while (getline(ifs, tmp)) {
		try {
			int sep1 = tmp.find(",");
			cmd = tmp.substr(0, sep1);
			if (cmd == "MOVIE_FILENAME") {
				int sep2 = tmp.find_first_of(",\n\r", sep1 + 1, 3) - sep1 - 1;
				value = tmp.substr(sep1 + 1, sep2);
				value.erase(std::remove(value.begin(), value.end(), ' '), value.end());
				*__movie_filename = value;
				//printf("MOVIE_FILENAME:%s\n", __movie_filename->c_str());
			}
			else if (cmd == "CONNECTED_CAMERA_NUMBER") {
				int sep2 = tmp.find_first_of(",\n\r", sep1 + 1, 3) - sep1 - 1;
				*__max_camera_num = atoi(value.c_str());
				value = tmp.substr(sep1 + 1, sep2);
				*__max_camera_num = atoi(value.c_str());
				//printf("CONNECTED CAMERA NUMBER:%d\n", *__max_camera_num);
			}
		}
		catch(...){

		}
	}
	return true;
}


/* **************************************************************** */
/* ******************** �^�X�N�g���C�̐ݒ� ************************ */
/* **************************************************************** */

// �g���C�̃{�^�����������ۂ̃R�[���o�b�N
void finish_cb(tray_menu* item);
void update_wallpaper_toggle_cb(tray_menu* item);
void set_fps_cb(tray_menu* item);
void set_camera_type_cb(tray_menu* item);

class MyTray {
	struct tray tray;				
	tray_menu* main_menu = NULL;	

	// fps���j���[�֘A
	tray_menu* fps_menu = NULL;
	float* fps_cand = NULL;
	char** fps_cand_text = NULL;
	int fps_cand_num = 0;

	// �J�����ԍ����j���[�֘A
	tray_menu* camera_type_menu = NULL;
	int* camera_type_cand = NULL;
	char** camera_type_cand_text = NULL;
	int camera_type_cand_num = 0;

	bool __create_fps_menu(float* fps_list, int fps_list_count) {
		if (fps_menu != NULL) return false;
		fps_cand_num = fps_list_count;
		fps_menu = new tray_menu[fps_cand_num * 2];
		fps_cand = new float[fps_cand_num];
		fps_cand_text = new char* [fps_cand_num];
		for (int i = 0; i < fps_cand_num * 2; i++) {
			fps_menu[i].disabled = 0;
			fps_menu[i].checked = 0;
			fps_menu[i].cb = nullptr;
			fps_menu[i].context = nullptr;
			fps_menu[i].context2 = nullptr;
			fps_menu[i].text = NULL;
			fps_menu[i].submenu = NULL;
		}
		for (int i = 0; i < fps_cand_num - 1; i++) {
			fps_menu[2 * i + 1].text = (char*)"-";
		}
		for (int i = 0; i < fps_cand_num; i++) {
			fps_cand[i] = fps_list[i];
			fps_cand_text[i] = new char[10];
			sprintf_s(fps_cand_text[i], 10, "%.1f", fps_cand[i]);

			fps_menu[2 * i].text = (char*)fps_cand_text[i];
			fps_menu[2 * i].cb = set_fps_cb;
			fps_menu[2 * i].context = this;
			fps_menu[2 * i].context2 = &(fps_cand[i]);
		}
		fps_menu[2].checked = 1;
		return true;
	}

	bool __create_camera_type_menu(int connected_camera_num) {
		if (camera_type_menu != NULL) return false;
		camera_type_cand_num = connected_camera_num + 1;	// +1 : ����t�@�C��

		camera_type_menu = new tray_menu[camera_type_cand_num * 2];
		camera_type_cand = new int[camera_type_cand_num];
		camera_type_cand_text = new char* [camera_type_cand_num];
		for (int i = 0; i < camera_type_cand_num * 2; i++) {
			camera_type_menu[i].disabled = 0;
			camera_type_menu[i].checked = 0;
			camera_type_menu[i].cb = nullptr;
			camera_type_menu[i].context = nullptr;
			camera_type_menu[i].context2 = nullptr;
			camera_type_menu[i].text = NULL;
			camera_type_menu[i].submenu = NULL;
		}
		for (int i = 0; i < camera_type_cand_num - 1; i++) {
			camera_type_menu[2 * i + 1].text = (char*)"-";
		}
		for (int i = 0; i < camera_type_cand_num; i++) {
			camera_type_cand[i] = i;
			camera_type_cand_text[i] = new char[12];
			if (i == camera_type_cand_num - 1)
				sprintf_s(camera_type_cand_text[i], 12, "movie file");
			else
				sprintf_s(camera_type_cand_text[i], 12, "camera %d", camera_type_cand[i] + 1);
			camera_type_menu[2 * i].text = (char*)camera_type_cand_text[i];
			camera_type_menu[2 * i].cb = set_camera_type_cb;
			camera_type_menu[2 * i].context = this;
			camera_type_menu[2 * i].context2 = &(camera_type_cand[i]);
		}
		camera_type_menu[0].checked = 1;
	}

	void __create_main_menu() {
		if (main_menu != NULL) delete[] main_menu;
		main_menu = new tray_menu[8];
		for (int i = 0; i < 8; i++) {
			main_menu[i].disabled = 0;
			main_menu[i].checked = 0;
			main_menu[i].cb = nullptr;
			main_menu[i].context = nullptr;
			main_menu[i].context2 = nullptr;
			main_menu[i].text = NULL;
			main_menu[i].submenu = NULL;
		}
		for (int i = 0; i < 3; i++) {
			main_menu[2 * i + 1].text = (char*)"-";
		}
		main_menu[0].text = (char*)"set fps";
		main_menu[0].submenu = fps_menu;
		main_menu[2].text = (char*)"select camera";
		main_menu[2].submenu = camera_type_menu;
		main_menu[4].text = (char*)"update wallpaper";
		main_menu[4].cb = update_wallpaper_toggle_cb;
		main_menu[4].context = this;
		main_menu[6].text = (char*)"exit";
		main_menu[6].cb = finish_cb;
		main_menu[6].context = this;
	}


public:
	MyTray(char* icon_filename, int connected_camera_num, float* fps_list, int fps_list_count) {
		//ClearAll();
		__create_fps_menu(fps_list, fps_list_count);
		__create_camera_type_menu(connected_camera_num);
		__create_main_menu();
		tray.icon = icon_filename;
		tray.menu = main_menu;
	}
	~MyTray() {
		ClearAll();
	}
	void ClearAll() {
		if (fps_menu != NULL) { delete[] fps_menu; fps_menu = NULL; }
		if (fps_cand != NULL) { delete[] fps_cand; fps_cand = NULL; }
		if (fps_cand_text != NULL) {
			for (int i = 0; i < fps_cand_num; i++) {
				delete[] fps_cand_text[i];
				fps_cand_text[i] = NULL;
			}
			delete[] fps_cand_text;
			fps_cand_text = NULL;
		}
		if (camera_type_menu != NULL) { delete[] camera_type_menu; camera_type_menu = NULL; }
		if (camera_type_cand != NULL) { delete[] camera_type_cand; camera_type_cand = NULL; }
		if (camera_type_cand_text != NULL) {
			for (int i = 0; i < camera_type_cand_num; i++) {
				delete[] camera_type_cand_text[i];
				camera_type_cand_text[i] = NULL;
			}
			delete[] camera_type_cand_text;
			camera_type_cand_text = NULL;
		}
	}


	// �R�[���o�b�N�֐�
	void FinishProcess(tray_menu* item, volatile bool* process_done_ptr) {
		*process_done_ptr = true;
		tray_exit();
	}
	void ToggleState(tray_menu* item, volatile bool* update_wallpaper_ptr) {
		bool tmp = !item->checked;
		*update_wallpaper_ptr = tmp;
		item->checked = tmp;
		tray_update(&tray);
	}
	void SetFps(tray_menu* item, volatile float* fps_ptr) {
		*fps_ptr = *((float*)(item->context2));
		// ����fps_menu��check���O��
		for (int i = 0; i < 4; i++) {
			fps_menu[2 * i].checked = 0;
		}
		// ������fps����check����
		item->checked = true;
		tray_update(&tray);
	};
	void SetCameraType(tray_menu* item, volatile int* camera_num_ptr) {
		*camera_num_ptr = *((int*)(item->context2));
		// ����camera_type_menu��check���O��
		for (int i = 0; i < camera_type_cand_num; i++)
			camera_type_menu[2 * i].checked = 0;
		// ������camera_type����check����
		item->checked = true;
		tray_update(&tray);
	}
	void Init() {
		tray_init(&tray);
	}
	int Roop(int i) {
		return tray_loop(i);
	}
};
MyTray* my_tray = NULL;


void finish_cb(tray_menu* item) {
	((MyTray*)item->context)->FinishProcess(item, &is_process_done);
}
void update_wallpaper_toggle_cb(tray_menu* item) {
	((MyTray*)item->context)->ToggleState(item, &is_update_wallpaper);
}
void set_fps_cb(tray_menu* item) {
	((MyTray*)item->context)->SetFps(item, &fps);
}
void set_camera_type_cb(tray_menu* item) {
	((MyTray*)item->context)->SetCameraType(item, &camera_num);
}



/* ************************************************************************ */
/* ***** �E�B���h�E�̈ʒu��T�C�Y���擾���違�r�b�g�}�b�v�̐ݒ������ ***** */
/* ***** �f�B�X�v���C����������ꍇ�͂��ꂼ��擾�A�ݒ肷��@�@�@�@ ***** */
/* ************************************************************************ */

// �e���j�^�̏����擾���A��������Ƃɓ�����������֐�
BOOL CALLBACK __EnumDisplayMonitorsProc(HMONITOR hMon, HDC hdcMon, LPRECT lpMon, LPARAM dwDate);

// ���j�^�S�̂��Ǘ�����N���X
class MONITORS {
private:
	int num;				// ���j�^�̐�
	int xoffset;			// user32��gdi32�ԁH�̃I�t�Z�b�g
	int yoffset;			// user32��gdi32�ԁH�̃I�t�Z�b�g
	int __tmp;				// CALLBACK�ƘA�g�p��temp
	RECT* rect;				// �f�B�X�v���C�̎l���̈ʒu
	BITMAPINFO* bitinfo;	// �r�b�g�}�b�v�̐ݒ�

	// EnumDisplayMonitors�̃R�[���o�b�N�֐����t�����h�Ɏw��
	friend BOOL CALLBACK __EnumDisplayMonitorsProc(HMONITOR hMon, HDC hdcMon, LPRECT lpMon, LPARAM dwDate);

	// EnumDisplayMonitors�ł̓v���C�}�����j�^�̍����(0, 0)�Ƃ��Ă���B
	// ����AStretchDIBits�̓��j�^�̒��ň�ԍ���̍��W��(0, 0)�Ƃ��Ă���B
	// �����̈Ⴂ��␳����B
	void __RefineMonitorPos() {
		// ���j�^�̒��ň�ԍ���̍��W���擾����
		if (num <= 0) return;
		xoffset = rect[0].left;
		yoffset = rect[0].top;
		for (int i = 0; i < num; i++) {
			if (xoffset > rect[i].left) xoffset = rect[i].left;
			if (yoffset > rect[i].top) yoffset = rect[i].top;
		}
		// (minX, minY)�����_(0, 0)�Ƃ��Ă��̑���rect�̒l���C������
		for (int i = 0; i < num; i++) {
			rect[i].left -= xoffset;
			rect[i].right -= xoffset;
			rect[i].top -= yoffset;
			rect[i].bottom -= yoffset;
		}
	}
	// �������p
	void __Clear() {
		num = 0;
		xoffset = 0;
		yoffset = 0;
		__tmp = 0;
		if (rect != NULL) { delete[] rect; rect = NULL; }
		//if (bitmap != NULL) { delete[] bitmap; bitmap = NULL; }
		if (bitinfo != NULL) { delete[] bitinfo; bitinfo = NULL; }
	}

public:
	MONITORS() {
		__Clear();
	}
	~MONITORS() {
		__Clear();
	}
	// ���j�^�̏����擾���A�����ݒ肷��
	void GetMonitor() {
		__Clear();
		num = GetSystemMetrics(SM_CMONITORS);
		rect = new RECT[num];
		//bitmap = new cv::Mat[num];
		bitinfo = new BITMAPINFO[num];
		SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
		// �e���j�^�̏����擾����
		EnumDisplayMonitors(NULL, NULL, __EnumDisplayMonitorsProc, (LPARAM)this);
		// ��ԍ���̍��W�ɍ��킹��
		__RefineMonitorPos();
	}
	// �Q�b�^�[
	int GetWidth(int n = 0) {
		if (n < 0 || n >= num) return -1;
		return rect[n].right - rect[n].left;
	}
	int GetHeight(int n = 0) {
		if (n < 0 || n >= num) return -1;
		return rect[n].bottom - rect[n].top;
	}
	int GetXPos(int n = 0) {
		if (n < 0 || n >= num) return -1;
		return rect[n].left;
	}
	int GetYPos(int n = 0) {
		if (n < 0 || n >= num) return -1;
		return rect[n].top;
	}
	BITMAPINFO* GetBitInfo(int n = 0) {
		if (n < 0 || n >= num) return NULL;
		return &(bitinfo[n]);
	}
	int GetMonitorNum() {
		return num;
	}
	// ���z�f�B�X�v���C��ł̓_���A�ǂ̃��j�^�[�̂ǂ��ɂ��邩���Q�b�g����
	bool WindowPosToBitMapPos(POINT virtual_desktop_pos, int* monitor_num, float* xpos, float* ypos) {
		// mouse_pos���ǂ̃��j�^�[��̓_�Ȃ̂���c������
		float mx = virtual_desktop_pos.x - xoffset;
		float my = virtual_desktop_pos.y - yoffset;
		*monitor_num = -1;
		*xpos = -1;
		*ypos = -1;
		for (int i = 0; i < num; i++) {
			if (mx < rect[i].right && mx > rect[i].left
				&& my < rect[i].bottom && my > rect[i].top) {
				// 0 ~ 1�͈̔͂ŕԂ� 0:���[or��[�A1:�E�[or���[
				*xpos = (mx - rect[i].left) / (rect[i].right - rect[i].left);
				*ypos = (my - rect[i].top) / (rect[i].bottom - rect[i].top);
				*monitor_num = i;
			}
		}
		return true;
	}
	// ���j�^n�̕ǎ�(hdc)�ɉ摜(src)��`�悷��
	bool DrawImage(int n, cv::Mat src, HDC hdc) {
		if (n < 0 || n >= num) return false;
		// �T�C�Y�����j�^��ʂɍ��킹��
		cv::resize(src, src, cv::Size(GetWidth(n), GetHeight(n)));
		// CV_8UC3 = > CV_32FC3
		cv::Mat draw_img = cv::Mat(src.size(), CV_32FC3);
		draw_img = src;
		// �ǎ��ɃJ�����摜��\��t��
		StretchDIBits(hdc, rect[n].left, rect[n].top, GetWidth(n), GetHeight(n),
			0, 0, draw_img.cols, draw_img.rows, draw_img.data,
			&(bitinfo[n]), DIB_RGB_COLORS, SRCCOPY);
		return true;
	}
};
BOOL CALLBACK __EnumDisplayMonitorsProc(HMONITOR hMon, HDC hdcMon, LPRECT lpMon, LPARAM dwDate) {
	MONITORS* mon = (MONITORS*)dwDate;
	mon->rect[mon->__tmp].bottom = lpMon->bottom;
	mon->rect[mon->__tmp].left = lpMon->left;
	mon->rect[mon->__tmp].top = lpMon->top;
	mon->rect[mon->__tmp].right = lpMon->right;
	// �r�b�g�}�b�v�̐ݒ�
	mon->bitinfo[mon->__tmp].bmiHeader.biBitCount = 24;
	mon->bitinfo[mon->__tmp].bmiHeader.biWidth = lpMon->right - lpMon->left;
	mon->bitinfo[mon->__tmp].bmiHeader.biHeight = -(lpMon->bottom - lpMon->top);
	mon->bitinfo[mon->__tmp].bmiHeader.biPlanes = 1;
	mon->bitinfo[mon->__tmp].bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	mon->bitinfo[mon->__tmp].bmiHeader.biCompression = BI_RGB;
	mon->bitinfo[mon->__tmp].bmiHeader.biClrImportant = 0;
	mon->bitinfo[mon->__tmp].bmiHeader.biClrUsed = 0;
	mon->bitinfo[mon->__tmp].bmiHeader.biSizeImage = 0;
	mon->bitinfo[mon->__tmp].bmiHeader.biXPelsPerMeter = 0;
	mon->bitinfo[mon->__tmp].bmiHeader.biYPelsPerMeter = 0;
	++(mon->__tmp);
	return TRUE;
}


/* ************************************************************************ */
/* ******************** �ǎ��̃E�B���h�E�n���h����T�� ******************** */
/* ************************************************************************ */
BOOL CALLBACK __EnumWindowsProc(HWND hwnd, LPARAM lp) {
	HWND* __wall_paper_handle = (HWND*)lp;
	// hwnd (iteration WorkerW��z��) ���q����SHELLDLL_FefView�E�B���h�E������������
	HWND shell = FindWindowEx(hwnd, NULL, "SHELLDLL_DefView", NULL);
	// hwnd (iteration WorkerW��z��) ���q����SHELLDLL_FefView�E�B���h�E�����ꍇ
	if (shell != NULL)
		// SHELLDLL_Def�̎���WorkerW�ɕ`�悷��΂悢
		*__wall_paper_handle = FindWindowEx(NULL, hwnd, "WorkerW", NULL);
	return TRUE;
}
HWND GetWallPaperWindowHandle() {
	HWND wall_paper_handle = NULL;
	EnumWindows(__EnumWindowsProc, (LPARAM)(&wall_paper_handle));
	// �����܂łŌ������Ă��Ȃ��ꍇ�A�t�H���_�ƕǎ����ꏏ�ɊǗ�����Ă���ƍl������
	// �ꉞ�AProgman=>SHELLDLL_DefView=>SysLIstView32�ɕ`�悷��ƕǎ����ς�邪�A�t�H���_���Ə����Ă��܂�
	//if (wall_paper_handle == NULL) {
	//	HWND progman = FindWindowEx(NULL, NULL, "Progman", NULL);
	//	HWND shell = FindWindowEx(progman, NULL, "SHELLDLL_DefView", NULL);
	//	wall_paper_handle = FindWindowEx(shell, NULL, "SysListView32", NULL);
	//}
	return wall_paper_handle;
}


/* ************************************************************************ */
/* ************************** ���̕ǎ��Ɋւ��鏈�� ************************ */
/* ************************************************************************ */

// BMP��ۑ�����֐�
// KrK (Knuth for Kludge)���̃v���O�����𗘗p�����Ă���������
// URL : https://www.ne.jp/asahi/krk/kct/programming/saveimagefile.htm
VOID SaveImageFile(HBITMAP hBmp, const char* filename)
{
	LONG imageSize;       // �摜�T�C�Y
	BITMAPFILEHEADER fh;  // �r�b�g�}�b�v�t�@�C���w�b�_
	BITMAPINFO* pbi;      // �r�b�g�}�b�v���
	BITMAP bmp = { 0 };    // �r�b�g�}�b�v�\����
	LONG bpp;             // ��f��
	LPBYTE bits;          // �摜�r�b�g
	HDC hdc;              // �f�o�C�X�R���e�L�X�g
	HDC hdc_mem;          // �f�o�C�X�R���e�L�X�g�E������
	HANDLE hFile;         // �t�@�C���n���h��
	DWORD writeSize;      // �������񂾃T�C�Y

	// BITMAP�����擾����
	GetObject(hBmp, sizeof(bmp), &bmp);
	hdc = GetDC(0);
	hdc_mem = CreateCompatibleDC(hdc);
	ReleaseDC(0, hdc);
	SelectObject(hdc_mem, hBmp);

	// �t�@�C���T�C�Y�v�Z
	imageSize = bmp.bmWidthBytes * bmp.bmHeight
		+ sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
	switch (bmp.bmBitsPixel)
	{
	case 2:
		bpp = 2;
		break;
	case 4:
		bpp = 16;
		break;
	case 8:
		bpp = 256;
		break;
	default:
		bpp = 0;
	}
	imageSize += (sizeof(RGBQUAD) * bpp);

	// BITMAPFILEHEADER�w�b�_�[�o��
	ZeroMemory(&fh, sizeof(fh));
	memcpy(&fh.bfType, "BM", 2);
	fh.bfSize = imageSize;
	fh.bfReserved1 = 0;
	fh.bfReserved2 = 0;
	fh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)
		+ sizeof(RGBQUAD) * bpp;

	// BITMAPINFOHEADER�w�b�_�[�o��
	pbi = new BITMAPINFO[sizeof(BITMAPINFOHEADER) + sizeof(RGBQUAD) * bpp];
	ZeroMemory(pbi, sizeof(BITMAPINFOHEADER));
	pbi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	pbi->bmiHeader.biWidth = bmp.bmWidth;
	pbi->bmiHeader.biHeight = bmp.bmHeight;
	pbi->bmiHeader.biPlanes = 1;
	pbi->bmiHeader.biBitCount = bmp.bmBitsPixel;
	pbi->bmiHeader.biCompression = BI_RGB;
	if (bpp != 0)
	{
		GetDIBColorTable(hdc_mem, 0, bpp, pbi->bmiColors);
	}

	// �摜�f�[�^�𓾂�
	bits = new BYTE[bmp.bmWidthBytes * bmp.bmHeight];
	GetDIBits(hdc_mem, hBmp, 0, bmp.bmHeight, bits, pbi, DIB_RGB_COLORS);

	// �t�@�C���ɏ�������
	hFile = CreateFile(filename, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, NULL);
	WriteFile(hFile, &fh, sizeof(fh), &writeSize, NULL);
	WriteFile(hFile, pbi,
		sizeof(BITMAPINFOHEADER) + sizeof(RGBQUAD) * bpp, &writeSize, NULL);
	WriteFile(hFile, bits, bmp.bmWidthBytes * bmp.bmHeight, &writeSize, NULL);
	CloseHandle(hFile);

	// �J��
	delete[] pbi;
	delete[] bits;
	DeleteDC(hdc_mem);
}
// ���̕ǎ���save_filename(BMP�`��)�ɕۑ�����֐�
bool SaveOriginalWallPaper(HWND wall_paper_handle, const char* save_filename) {
	RECT rect;
	GetWindowRect(wall_paper_handle, &rect);
	int width = rect.right - rect.left;
	int height = rect.bottom - rect.top;
	HDC wall_paper_hdc = GetDCEx(wall_paper_handle, NULL, 0x403);
	HDC copyed_hdc = CreateCompatibleDC(wall_paper_hdc);
	HBITMAP hBitmap = CreateCompatibleBitmap(wall_paper_hdc, width, height);
	// �悭�킩��Ȃ�	
	HGDIOBJ hOld = SelectObject(copyed_hdc, hBitmap);
	BitBlt(copyed_hdc, 0, 0, width, height, wall_paper_hdc, 0, 0, SRCCOPY);
	SelectObject(copyed_hdc, hOld);
	DeleteDC(copyed_hdc);
	ReleaseDC(wall_paper_handle, wall_paper_hdc);
	SaveImageFile(hBitmap, save_filename);
	DeleteObject(hBitmap);
	return true;
}
// �ǎ������̕ǎ��ioriginal_filename�j�ɖ߂��֐�
bool ReloadOriginalWallPaper(HWND wall_paper_handle, const char* original_filename) {
	HBITMAP original_wall_paper_bitmap = (HBITMAP)(HBITMAP)LoadImage(NULL, original_filename,
		IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);;
	RECT rect;
	GetWindowRect(wall_paper_handle, &rect);
	int width = rect.right - rect.left;
	int height = rect.bottom - rect.top;
	HDC wall_paper_hdc = GetDCEx(wall_paper_handle, NULL, 0x403);
	HDC copyed_hdc = CreateCompatibleDC(wall_paper_hdc);
	SelectObject(copyed_hdc, original_wall_paper_bitmap);
	BitBlt(wall_paper_hdc, 0, 0, width, height, copyed_hdc, 0, 0, SRCCOPY);
	DeleteDC(copyed_hdc);
	ReleaseDC(wall_paper_handle, wall_paper_hdc);
	DeleteObject(original_wall_paper_bitmap);
	return true;
}



/* ************************************************************************ */
/* ****************************** ���C�����[�v **************************** */
/* ************************************************************************ */

void MainRoop(HWND wall_paper_handle) {

	// ���j�^�̌��A�T�C�Y�A�ʒu�֌W�Ȃǂ��擾�A�ݒ肷��
	MONITORS monitors;
	monitors.GetMonitor();

	// �J�����̐ݒ蓙
	int current_camera_num = 0;
	cv::VideoCapture* cap = NULL;
	cv::Mat frame;

	// tray��Exit���������܂Ń��[�v����
	while (!is_process_done) {

		// fps�Ǘ��p
		std::chrono::system_clock::time_point start = std::chrono::system_clock::now();

		// �w�i�摜���A�b�v�f�[�g���Ȃ��ꍇ
		if (!is_update_wallpaper) {
			// cap��NULL�łȂ����cap���~������
			if (cap != NULL) {
				cap->release();
				delete cap;
				cap = NULL;
			}
			Sleep(500);
			continue;
		}

		// �͂��߂ăJ�����𓮂����� or �J�����̎�ނ�ς��鎞
		if (cap == NULL || camera_num != current_camera_num) {
			if (cap != NULL) {
				cap->release();
				delete cap;
			}
			current_camera_num = camera_num;
			// ����t�@�C����ǂݍ��ޏꍇ
			if (camera_num == max_camera_num)
				cap = new cv::VideoCapture(movie_filename);
			// �J�������g���ꍇ
			else
				cap = new cv::VideoCapture(camera_num);
		}

		// �J�����摜��荞�݁A�J�������g���Ȃ��ꍇ�͐��摜��Ԃ�
		if (!cap->read(frame)) {
			bool tmp_flag = true;
			// ����t�@�C�����Đ����Ă���ꍇ�A�����߂��Ă�����x�`�F�b�N����
			// �{����fps�ɍ��킹��cap->read()��ς��Ȃ��Ƃ����Ȃ���������
			if (camera_num == max_camera_num) {
				cap->set(cv::CAP_PROP_POS_FRAMES, 0);
				if (cap->read(frame))
					tmp_flag = false;
			}
			if (tmp_flag) {
				// �J�������g���Ȃ����Ƃ��A�s�[��
				frame = cv::Mat(cv::Size(1000, 500), CV_8UC3, cv::Scalar(255, 0, 0));
				std::string msg = (camera_num == max_camera_num) ? movie_filename + std::string("is not opened")
					: "Camera" + std::to_string(camera_num + 1) + " is not opened";
				cv::putText(frame, msg, cv::Point(30, 40), 0, 1, cv::Scalar(0, 0, 255), 1);
			}
		}

		// �}�E�X�J�[�\���̈ʒu���擾����
		// ���j�^(monitor_idx)��(xpos*Width, ypos*Height)�Ƀ}�E�X�����邱�Ƃ�����
		int monitor_idx;
		float xpos, ypos;
		POINT po;
		GetCursorPos(&po);
		monitors.WindowPosToBitMapPos(po, &monitor_idx, &xpos, &ypos);

		// �ǎ��̃f�o�C�X�R���e�L�X�g�̎擾
		HDC wall_paper_hdc = GetDC(wall_paper_handle);

		// �e���j�^�ɑ΂��ăJ�����摜��\��t������
		for (int i = 0; i < monitors.GetMonitorNum(); i++) {

			// �ʃ��j�^���Ƃɕ\���摜�ς������Ȃ�A������frame�ɗ�����������
			// e.g.) �}�E�X�J�[�\���̈ʒu�����F�ۂŕ\������
			cv::Mat dst = frame.clone();
			if (monitor_idx == i)
				cv::circle(dst, cv::Size((int)(xpos * dst.cols), (int)(ypos * dst.rows)),
					(int)(dst.cols * 0.005 + 0.5), cv::Scalar(0, 255, 255), -1);

			// ���j�^i�ɉ摜frame or dst��`�悷��
			//monitors.DrawImage(i, frame, wall_paper_hdc);	  // �S�Ẵ��j�^�ɓ������e��`�悷��Ȃ炱����
			monitors.DrawImage(i, dst, wall_paper_hdc);		  // �e���j�^���Ƃɕ`����e��ς���Ȃ炱����
		}
		// �f�o�C�X�R���e�L�X�g�̃����[�X
		ReleaseDC(wall_paper_handle, wall_paper_hdc);

		std::chrono::system_clock::time_point end = std::chrono::system_clock::now();
		double elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
		int wait_time = (int)(1000.0 / fps) - elapsed;
		if (wait_time > 0 && wait_time < 1000)
			Sleep(wait_time);  // CPU�g�p�����l�����X���[�v�B
	}

	if (cap != NULL) {
		cap->release();
		delete cap;
	}
}



void WriteLogFile(const char* msg) {
	FILE* fp = NULL;
	fopen_s(&fp, LOG_FILENAME, "w");
	if (fp != NULL) {
		fprintf_s(fp, msg);
		fclose(fp);
	}
}


// ���C���֐�
// Debug���� int main(){�ɕς��āA�v���W�F�N�g�˃v���p�e�B�˃����J�˃V�X�e���˃T�u�V�X�e����Console�ɕς���
// exe�쐬���� int WinMain(*){�ɕς��āA�v���W�F�N�g�˃v���p�e�B�˃����J�˃V�X�e���˃T�u�V�X�e����Windows�ɕς���
//int main() {
int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {

	// �V�X�e�����������𑜓x���o�͂��邽�߂Ɏ��O�Ɏ��s���Ă����K�v������
	SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

	// �ǎ��iWorkerW�j�̃n���h���𓾂�B�Ȃ���ΏI���B
	HWND wall_paper_handle = GetWallPaperWindowHandle();
	if (wall_paper_handle == NULL) {
		WriteLogFile("�ǎ����݂���܂���ł����B\nspy++.exe��Progman��WorkerW����������܂ŕǎ���K���ɕς��Ă݂Ă��������B\n");
		return -1;
	}

	// ���݂̕ǎ���ۑ�����B�I�����ɕK�v�B
	const char* original_filename = "DONT_DELETE_original_wallpaper.bmp";
	if (!SaveOriginalWallPaper(wall_paper_handle, original_filename)) {
		WriteLogFile("���݂̕ǎ��𐳂����R�s�[�ł��܂���ł����B\n");
		return -1;
	}

	// �ݒ�t�@�C���̃��[�h
	LoadConfigFile(&max_camera_num, &movie_filename);

	// �g���C��ݒ肷��
	float fps_list[4] = { 60, 30, 15, 7.5 };
	my_tray = new MyTray((char*)ICON_FILENAME, (int)max_camera_num, fps_list, 4);

	// ���C�����[�v�X�^�[�g
	// �g���C�̏I���{�^���������ꂽ��I��
	std::thread main_thread(MainRoop, wall_paper_handle);
	my_tray->Init();
	while (my_tray->Roop(1) == 0);
	main_thread.join();

	// �ǎ������Ƃɖ߂�
	if (!ReloadOriginalWallPaper(wall_paper_handle, original_filename)) {
		WriteLogFile("�ǎ������ɖ߂��܂���ł����B\n");
		return -1;
	}
	
	// �g���C�̉���
	delete my_tray;

	return 1;
}

