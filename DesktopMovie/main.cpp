// 参考にした記事
// https://qiita.com/Raitichan82/items/eee6ef7b5a37cf469740
// https://www.natsuneko.blog/entry/2016/12/31/wallpaper-engine-concept
// https://www.codeproject.com/Articles/856020/Draw-Behind-Desktop-Icons-in-Windows-plus
// https://qiita.com/takamon9/items/603a2956b640904e2043
// https://nodamushi.hatenablog.com/entry/2018/09/17/024153
// http://yamatyuu.net/computer/program/sdk/base/enumdisplay/index.html
// http://www.orangemaker.sakura.ne.jp/labo/memo/sdk-mfc/win7Desktop.html

// トレイ関連
#define TRAY_WINAPI 1
#include "tray.h"

#pragma comment(lib, "SHCore.lib")   // 実際の解像度を取得するために必要
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

// これをinitファイルから読み込む形式に変える
#define ICON_FILENAME "icon.png"		// トレイアイコンのファイル名
#define LOG_FILENAME "log.txt"			// ログファイルの名前
#define CONFIG_FILENAME "config.csv"	// 設定ファイルの名前


// global変数
volatile bool is_process_done = false;				// プロセス実行中or終了のフラグ
volatile bool is_update_wallpaper = false;			// 壁紙を更新するかどうか。はじめは更新しない
volatile int max_camera_num = 5;       				// PCに接続されているカメラの数
volatile int camera_num = 0;						// 使用するカメラの番号
volatile float fps = 30;							// fps
std::string movie_filename = "movie.mp4";			// 動画のファイル名


													/* **************************************************************** */
/* ********************* 設定ファイルのロード ********************* */
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
/* ******************** タスクトレイの設定 ************************ */
/* **************************************************************** */

// トレイのボタンを押した際のコールバック
void finish_cb(tray_menu* item);
void update_wallpaper_toggle_cb(tray_menu* item);
void set_fps_cb(tray_menu* item);
void set_camera_type_cb(tray_menu* item);

class MyTray {
	struct tray tray;				
	tray_menu* main_menu = NULL;	

	// fpsメニュー関連
	tray_menu* fps_menu = NULL;
	float* fps_cand = NULL;
	char** fps_cand_text = NULL;
	int fps_cand_num = 0;

	// カメラ番号メニュー関連
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
		camera_type_cand_num = connected_camera_num + 1;	// +1 : 動画ファイル

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


	// コールバック関数
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
		// 他のfps_menuのcheckを外す
		for (int i = 0; i < 4; i++) {
			fps_menu[2 * i].checked = 0;
		}
		// 自分のfpsだけcheckする
		item->checked = true;
		tray_update(&tray);
	};
	void SetCameraType(tray_menu* item, volatile int* camera_num_ptr) {
		*camera_num_ptr = *((int*)(item->context2));
		// 他のcamera_type_menuのcheckを外す
		for (int i = 0; i < camera_type_cand_num; i++)
			camera_type_menu[2 * i].checked = 0;
		// 自分のcamera_typeだけcheckする
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
/* ***** ウィンドウの位置やサイズを取得する＆ビットマップの設定をする ***** */
/* ***** ディスプレイが複数個ある場合はそれぞれ取得、設定する　　　　 ***** */
/* ************************************************************************ */

// 各モニタの情報を取得し、それをもとに内部処理する関数
BOOL CALLBACK __EnumDisplayMonitorsProc(HMONITOR hMon, HDC hdcMon, LPRECT lpMon, LPARAM dwDate);

// モニタ全体を管理するクラス
class MONITORS {
private:
	int num;				// モニタの数
	int xoffset;			// user32とgdi32間？のオフセット
	int yoffset;			// user32とgdi32間？のオフセット
	int __tmp;				// CALLBACKと連携用のtemp
	RECT* rect;				// ディスプレイの四隅の位置
	BITMAPINFO* bitinfo;	// ビットマップの設定

	// EnumDisplayMonitorsのコールバック関数をフレンドに指定
	friend BOOL CALLBACK __EnumDisplayMonitorsProc(HMONITOR hMon, HDC hdcMon, LPRECT lpMon, LPARAM dwDate);

	// EnumDisplayMonitorsではプライマリモニタの左上を(0, 0)としている。
	// 一方、StretchDIBitsはモニタの中で一番左上の座標を(0, 0)としている。
	// これらの違いを補正する。
	void __RefineMonitorPos() {
		// モニタの中で一番左上の座標を取得する
		if (num <= 0) return;
		xoffset = rect[0].left;
		yoffset = rect[0].top;
		for (int i = 0; i < num; i++) {
			if (xoffset > rect[i].left) xoffset = rect[i].left;
			if (yoffset > rect[i].top) yoffset = rect[i].top;
		}
		// (minX, minY)を原点(0, 0)としてその他のrectの値を修正する
		for (int i = 0; i < num; i++) {
			rect[i].left -= xoffset;
			rect[i].right -= xoffset;
			rect[i].top -= yoffset;
			rect[i].bottom -= yoffset;
		}
	}
	// 初期化用
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
	// モニタの情報を取得し、初期設定する
	void GetMonitor() {
		__Clear();
		num = GetSystemMetrics(SM_CMONITORS);
		rect = new RECT[num];
		//bitmap = new cv::Mat[num];
		bitinfo = new BITMAPINFO[num];
		SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
		// 各モニタの情報を取得する
		EnumDisplayMonitors(NULL, NULL, __EnumDisplayMonitorsProc, (LPARAM)this);
		// 一番左上の座標に合わせる
		__RefineMonitorPos();
	}
	// ゲッター
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
	// 仮想ディスプレイ上での点が、どのモニターのどこにあるかをゲットする
	bool WindowPosToBitMapPos(POINT virtual_desktop_pos, int* monitor_num, float* xpos, float* ypos) {
		// mouse_posがどのモニター上の点なのかを把握する
		float mx = virtual_desktop_pos.x - xoffset;
		float my = virtual_desktop_pos.y - yoffset;
		*monitor_num = -1;
		*xpos = -1;
		*ypos = -1;
		for (int i = 0; i < num; i++) {
			if (mx < rect[i].right && mx > rect[i].left
				&& my < rect[i].bottom && my > rect[i].top) {
				// 0 ~ 1の範囲で返す 0:左端or上端、1:右端or下端
				*xpos = (mx - rect[i].left) / (rect[i].right - rect[i].left);
				*ypos = (my - rect[i].top) / (rect[i].bottom - rect[i].top);
				*monitor_num = i;
			}
		}
		return true;
	}
	// モニタnの壁紙(hdc)に画像(src)を描画する
	bool DrawImage(int n, cv::Mat src, HDC hdc) {
		if (n < 0 || n >= num) return false;
		// サイズをモニタ画面に合わせる
		cv::resize(src, src, cv::Size(GetWidth(n), GetHeight(n)));
		// CV_8UC3 = > CV_32FC3
		cv::Mat draw_img = cv::Mat(src.size(), CV_32FC3);
		draw_img = src;
		// 壁紙にカメラ画像を貼り付け
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
	// ビットマップの設定
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
/* ******************** 壁紙のウィンドウハンドルを探す ******************** */
/* ************************************************************************ */
BOOL CALLBACK __EnumWindowsProc(HWND hwnd, LPARAM lp) {
	HWND* __wall_paper_handle = (HWND*)lp;
	// hwnd (iteration WorkerWを想定) が子供にSHELLDLL_FefViewウィンドウを持つかを検索
	HWND shell = FindWindowEx(hwnd, NULL, "SHELLDLL_DefView", NULL);
	// hwnd (iteration WorkerWを想定) が子供にSHELLDLL_FefViewウィンドウを持つ場合
	if (shell != NULL)
		// SHELLDLL_Defの次のWorkerWに描画すればよい
		*__wall_paper_handle = FindWindowEx(NULL, hwnd, "WorkerW", NULL);
	return TRUE;
}
HWND GetWallPaperWindowHandle() {
	HWND wall_paper_handle = NULL;
	EnumWindows(__EnumWindowsProc, (LPARAM)(&wall_paper_handle));
	// ここまでで見つかっていない場合、フォルダと壁紙が一緒に管理されていると考えられる
	// 一応、Progman=>SHELLDLL_DefView=>SysLIstView32に描画すると壁紙が変わるが、フォルダごと消えてしまう
	//if (wall_paper_handle == NULL) {
	//	HWND progman = FindWindowEx(NULL, NULL, "Progman", NULL);
	//	HWND shell = FindWindowEx(progman, NULL, "SHELLDLL_DefView", NULL);
	//	wall_paper_handle = FindWindowEx(shell, NULL, "SysListView32", NULL);
	//}
	return wall_paper_handle;
}


/* ************************************************************************ */
/* ************************** 元の壁紙に関する処理 ************************ */
/* ************************************************************************ */

// BMPを保存する関数
// KrK (Knuth for Kludge)氏のプログラムを利用させていただいた
// URL : https://www.ne.jp/asahi/krk/kct/programming/saveimagefile.htm
VOID SaveImageFile(HBITMAP hBmp, const char* filename)
{
	LONG imageSize;       // 画像サイズ
	BITMAPFILEHEADER fh;  // ビットマップファイルヘッダ
	BITMAPINFO* pbi;      // ビットマップ情報
	BITMAP bmp = { 0 };    // ビットマップ構造体
	LONG bpp;             // 画素数
	LPBYTE bits;          // 画像ビット
	HDC hdc;              // デバイスコンテキスト
	HDC hdc_mem;          // デバイスコンテキスト・メモリ
	HANDLE hFile;         // ファイラハンドル
	DWORD writeSize;      // 書き込んだサイズ

	// BITMAP情報を取得する
	GetObject(hBmp, sizeof(bmp), &bmp);
	hdc = GetDC(0);
	hdc_mem = CreateCompatibleDC(hdc);
	ReleaseDC(0, hdc);
	SelectObject(hdc_mem, hBmp);

	// ファイルサイズ計算
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

	// BITMAPFILEHEADERヘッダー出力
	ZeroMemory(&fh, sizeof(fh));
	memcpy(&fh.bfType, "BM", 2);
	fh.bfSize = imageSize;
	fh.bfReserved1 = 0;
	fh.bfReserved2 = 0;
	fh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)
		+ sizeof(RGBQUAD) * bpp;

	// BITMAPINFOHEADERヘッダー出力
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

	// 画像データを得る
	bits = new BYTE[bmp.bmWidthBytes * bmp.bmHeight];
	GetDIBits(hdc_mem, hBmp, 0, bmp.bmHeight, bits, pbi, DIB_RGB_COLORS);

	// ファイルに書き込む
	hFile = CreateFile(filename, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, NULL);
	WriteFile(hFile, &fh, sizeof(fh), &writeSize, NULL);
	WriteFile(hFile, pbi,
		sizeof(BITMAPINFOHEADER) + sizeof(RGBQUAD) * bpp, &writeSize, NULL);
	WriteFile(hFile, bits, bmp.bmWidthBytes * bmp.bmHeight, &writeSize, NULL);
	CloseHandle(hFile);

	// 開放
	delete[] pbi;
	delete[] bits;
	DeleteDC(hdc_mem);
}
// 元の壁紙をsave_filename(BMP形式)に保存する関数
bool SaveOriginalWallPaper(HWND wall_paper_handle, const char* save_filename) {
	RECT rect;
	GetWindowRect(wall_paper_handle, &rect);
	int width = rect.right - rect.left;
	int height = rect.bottom - rect.top;
	HDC wall_paper_hdc = GetDCEx(wall_paper_handle, NULL, 0x403);
	HDC copyed_hdc = CreateCompatibleDC(wall_paper_hdc);
	HBITMAP hBitmap = CreateCompatibleBitmap(wall_paper_hdc, width, height);
	// よくわからない	
	HGDIOBJ hOld = SelectObject(copyed_hdc, hBitmap);
	BitBlt(copyed_hdc, 0, 0, width, height, wall_paper_hdc, 0, 0, SRCCOPY);
	SelectObject(copyed_hdc, hOld);
	DeleteDC(copyed_hdc);
	ReleaseDC(wall_paper_handle, wall_paper_hdc);
	SaveImageFile(hBitmap, save_filename);
	DeleteObject(hBitmap);
	return true;
}
// 壁紙を元の壁紙（original_filename）に戻す関数
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
/* ****************************** メインループ **************************** */
/* ************************************************************************ */

void MainRoop(HWND wall_paper_handle) {

	// モニタの個数、サイズ、位置関係などを取得、設定する
	MONITORS monitors;
	monitors.GetMonitor();

	// カメラの設定等
	int current_camera_num = 0;
	cv::VideoCapture* cap = NULL;
	cv::Mat frame;

	// trayでExitが押されるまでループする
	while (!is_process_done) {

		// fps管理用
		std::chrono::system_clock::time_point start = std::chrono::system_clock::now();

		// 背景画像をアップデートしない場合
		if (!is_update_wallpaper) {
			// capがNULLでなければcapを停止させる
			if (cap != NULL) {
				cap->release();
				delete cap;
				cap = NULL;
			}
			Sleep(500);
			continue;
		}

		// はじめてカメラを動かす時 or カメラの種類を変える時
		if (cap == NULL || camera_num != current_camera_num) {
			if (cap != NULL) {
				cap->release();
				delete cap;
			}
			current_camera_num = camera_num;
			// 動画ファイルを読み込む場合
			if (camera_num == max_camera_num)
				cap = new cv::VideoCapture(movie_filename);
			// カメラを使う場合
			else
				cap = new cv::VideoCapture(camera_num);
		}

		// カメラ画像取り込み、カメラが使えない場合は青い画像を返す
		if (!cap->read(frame)) {
			bool tmp_flag = true;
			// 動画ファイルを再生している場合、巻き戻してもう一度チェックする
			// 本当はfpsに合わせてcap->read()を変えないといけないが未実装
			if (camera_num == max_camera_num) {
				cap->set(cv::CAP_PROP_POS_FRAMES, 0);
				if (cap->read(frame))
					tmp_flag = false;
			}
			if (tmp_flag) {
				// カメラが使えないことをアピール
				frame = cv::Mat(cv::Size(1000, 500), CV_8UC3, cv::Scalar(255, 0, 0));
				std::string msg = (camera_num == max_camera_num) ? movie_filename + std::string("is not opened")
					: "Camera" + std::to_string(camera_num + 1) + " is not opened";
				cv::putText(frame, msg, cv::Point(30, 40), 0, 1, cv::Scalar(0, 0, 255), 1);
			}
		}

		// マウスカーソルの位置を取得する
		// モニタ(monitor_idx)で(xpos*Width, ypos*Height)にマウスがあることを示す
		int monitor_idx;
		float xpos, ypos;
		POINT po;
		GetCursorPos(&po);
		monitors.WindowPosToBitMapPos(po, &monitor_idx, &xpos, &ypos);

		// 壁紙のデバイスコンテキストの取得
		HDC wall_paper_hdc = GetDC(wall_paper_handle);

		// 各モニタに対してカメラ画像を貼り付けする
		for (int i = 0; i < monitors.GetMonitorNum(); i++) {

			// 個別モニタごとに表示画像変えたいなら、ここでframeに落書きをする
			// e.g.) マウスカーソルの位置を黄色丸で表示する
			cv::Mat dst = frame.clone();
			if (monitor_idx == i)
				cv::circle(dst, cv::Size((int)(xpos * dst.cols), (int)(ypos * dst.rows)),
					(int)(dst.cols * 0.005 + 0.5), cv::Scalar(0, 255, 255), -1);

			// モニタiに画像frame or dstを描画する
			//monitors.DrawImage(i, frame, wall_paper_hdc);	  // 全てのモニタに同じ内容を描画するならこっち
			monitors.DrawImage(i, dst, wall_paper_hdc);		  // 各モニタごとに描画内容を変えるならこっち
		}
		// デバイスコンテキストのリリース
		ReleaseDC(wall_paper_handle, wall_paper_hdc);

		std::chrono::system_clock::time_point end = std::chrono::system_clock::now();
		double elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
		int wait_time = (int)(1000.0 / fps) - elapsed;
		if (wait_time > 0 && wait_time < 1000)
			Sleep(wait_time);  // CPU使用率を考慮しスリープ。
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


// メイン関数
// Debug時は int main(){に変えて、プロジェクト⇒プロパティ⇒リンカ⇒システム⇒サブシステムをConsoleに変える
// exe作成時は int WinMain(*){に変えて、プロジェクト⇒プロパティ⇒リンカ⇒システム⇒サブシステムをWindowsに変える
//int main() {
int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {

	// システムが正しい解像度を出力するために事前に実行しておく必要がある
	SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

	// 壁紙（WorkerW）のハンドルを得る。なければ終了。
	HWND wall_paper_handle = GetWallPaperWindowHandle();
	if (wall_paper_handle == NULL) {
		WriteLogFile("壁紙がみつかりませんでした。\nspy++.exeでProgmanとWorkerWが分離するまで壁紙を適当に変えてみてください。\n");
		return -1;
	}

	// 現在の壁紙を保存する。終了時に必要。
	const char* original_filename = "DONT_DELETE_original_wallpaper.bmp";
	if (!SaveOriginalWallPaper(wall_paper_handle, original_filename)) {
		WriteLogFile("現在の壁紙を正しくコピーできませんでした。\n");
		return -1;
	}

	// 設定ファイルのロード
	LoadConfigFile(&max_camera_num, &movie_filename);

	// トレイを設定する
	float fps_list[4] = { 60, 30, 15, 7.5 };
	my_tray = new MyTray((char*)ICON_FILENAME, (int)max_camera_num, fps_list, 4);

	// メインループスタート
	// トレイの終了ボタンが押されたら終了
	std::thread main_thread(MainRoop, wall_paper_handle);
	my_tray->Init();
	while (my_tray->Roop(1) == 0);
	main_thread.join();

	// 壁紙をもとに戻す
	if (!ReloadOriginalWallPaper(wall_paper_handle, original_filename)) {
		WriteLogFile("壁紙を元に戻せませんでした。\n");
		return -1;
	}
	
	// トレイの解除
	delete my_tray;

	return 1;
}

