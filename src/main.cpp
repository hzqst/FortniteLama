#include <Windows.h>
#include <opencv2/opencv.hpp>
#include <opencv2/highgui.hpp>
#include <functional>
#include <vector>

#include "scope_exit.h"
#include "buffer.h"

using SnapshotCallback = std::function<void(void *, size_t, int, int, int)>;

virtual_buffer_t m_snapshot_buffer;

bool DIBSnapshot(HWND hWnd, int scale, const SnapshotCallback &callback)
{
	HDC hDC = GetDC(hWnd);
	if (!hDC)
	{
		return false;
	}

	const auto hDCScope = std::experimental::make_scope_exit([hWnd, hDC] { ReleaseDC(hWnd, hDC); });

	RECT rcScreen;

	if (hWnd == GetDesktopWindow())
	{
		rcScreen.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
		rcScreen.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
		rcScreen.right = GetSystemMetrics(SM_CXVIRTUALSCREEN);
		rcScreen.bottom = GetSystemMetrics(SM_CYVIRTUALSCREEN);
	}
	else
	{
		GetWindowRect(hWnd, &rcScreen);
	}

	int destW = (scale == 100) ? (rcScreen.right - rcScreen.left) : (rcScreen.right - rcScreen.left) * scale / 100;
	int destH = (scale == 100) ? (rcScreen.bottom - rcScreen.top) : (rcScreen.bottom - rcScreen.top) * scale / 100;

	BITMAPINFOHEADER bi;
	memset(&bi, 0, sizeof(bi));
	bi.biSize = sizeof(bi);
	bi.biWidth = destW;
	bi.biHeight = -destH;
	bi.biPlanes = 1;
	bi.biBitCount = 32;
	bi.biCompression = BI_RGB;

	PVOID pv = NULL;
	HBITMAP hBitmap = CreateDIBSection(hDC, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &pv, 0, 0);
	if (!hBitmap)
	{
		OutputDebugStringA("CreateDIBSection failed");
		return false;
	}
	const auto hBitmapScope = std::experimental::make_scope_exit([hBitmap] { DeleteObject(hBitmap); });

	HDC hMemDC = CreateCompatibleDC(hDC);
	if (!hMemDC)
	{
		OutputDebugStringA("CreateCompatibleDC failed");
		return false;
	}
	const auto hMemDCScope = std::experimental::make_scope_exit([hMemDC] { DeleteDC(hMemDC); });

	HBITMAP hOldBmp = (HBITMAP)SelectObject(hMemDC, hBitmap);

	const auto hOldBmpScope = std::experimental::make_scope_exit([hMemDC, hOldBmp] { SelectObject(hMemDC, hOldBmp); });

	if (scale != 100)
	{
		SetStretchBltMode(hMemDC, STRETCH_HALFTONE);

		if (!StretchBlt(hMemDC, 0, 0, destW, destH, hDC, 0, 0, rcScreen.right, rcScreen.bottom, SRCCOPY))
		{
			OutputDebugStringA("StretchBlt failed");
			return false;
		}
	}
	else
	{
		if (!BitBlt(hMemDC, 0, 0, rcScreen.right, rcScreen.bottom, hDC, 0, 0, SRCCOPY))
		{
			OutputDebugStringA("BitBlt failed");
			return false;
		}
	}

	BITMAP bitmap;
	if (!GetObjectW(hBitmap, sizeof(BITMAP), &bitmap))
	{
		OutputDebugStringA("GetObjectW failed");
		return false;
	}
	PVOID pBuffer = m_snapshot_buffer.GetSpace(bitmap.bmHeight * bitmap.bmWidthBytes);
	if (!pBuffer)
	{
		OutputDebugStringA("m_snapshot_buffer.GetSpace failed");
		return false;
	}

	if (GetBitmapBits(hBitmap, bitmap.bmHeight * bitmap.bmWidthBytes, pBuffer) <= 0)
	{
		OutputDebugStringA("GetBitmapBits failed");
		return false;
	}

	callback(pBuffer, bitmap.bmHeight * bitmap.bmWidthBytes, bitmap.bmWidth, bitmap.bmHeight, bitmap.bmBitsPixel);

	return true;
}

bool DIBToCvMat(cv::Mat &mat, void *pBuffer, size_t cbBuffer, int width, int height, int bbp)
{
	mat.create(height, width, CV_8UC3);

	int nChannels = bbp / 8;

	int nStep = nChannels * width;

	for (int nRow = 0; nRow < height; nRow++)
	{
		auto pucRow = (mat.ptr<uchar>(nRow));
		for (int nCol = 0; nCol < width; nCol++)
		{
			pucRow[nCol * 3 + 0] = *((uchar *)pBuffer + nRow * nStep + nCol * nChannels + 0);
			pucRow[nCol * 3 + 1] = *((uchar *)pBuffer + nRow * nStep + nCol * nChannels + 1);
			pucRow[nCol * 3 + 2] = *((uchar *)pBuffer + nRow * nStep + nCol * nChannels + 2);
		}
	}

	return true;
}

int MakeKeyLParam(int VirtualKey, int flag)
{
	UINT sCode;
	//Firstbyte ; lparam 参数的 24-31位
	UINT Firstbyte;
	switch (flag)
	{
	case WM_KEYDOWN:    Firstbyte = 0;   break;
	case WM_KEYUP:      Firstbyte = 0xC0; break;
	case WM_CHAR:       Firstbyte = 0x20; break;
	case WM_SYSKEYDOWN: Firstbyte = 0x20; break;
	case WM_SYSKEYUP:   Firstbyte = 0xE0; break;
	case WM_SYSCHAR:    Firstbyte = 0xE0; break;
	}
	// 键的扫描码; lparam 参数 的 16-23位
	// 16–23 Specifies the scan code. 
	UINT iKey = MapVirtualKeyW(VirtualKey, 0);
	// 1为 lparam 参数的 0-15位，即发送次数
	// 0–15 Specifies the repeat count for the current message. 
	sCode = (Firstbyte << 24) + 1 + (iKey << 16) + 1;
	return sCode;
}

void SimKeyClick(UINT vk_Code, BOOL bDown)
{
	DWORD dwFlages = 0;
	switch (vk_Code)
	{
	default:
		break;
	case(VK_NUMLOCK):
	case(VK_CAPITAL):
	case(VK_SCROLL):
	case(VK_CONTROL):
	case(VK_LCONTROL):
	case(VK_RCONTROL):
	case(VK_SHIFT):
	case(VK_LSHIFT):
	case(VK_RSHIFT):
	case(VK_MENU):
	case(VK_LMENU):
	case(VK_RMENU):
		dwFlages |= KEYEVENTF_EXTENDEDKEY;
	}
	WORD wScan = MapVirtualKeyW(vk_Code, 0);
	INPUT Input[1] = { 0 };
	Input[0].type = INPUT_KEYBOARD;
	Input[0].ki.wVk = vk_Code;
	Input[0].ki.wScan = wScan;
	Input[0].ki.dwFlags = (bDown) ? dwFlages : dwFlages | KEYEVENTF_KEYUP;
	SendInput(1, Input, sizeof(INPUT));
}

void SimMouseClick(BOOL bDown)
{
	INPUT input;
	input.type = INPUT_MOUSE;
	input.mi.dx = 0;
	input.mi.dy = 0;
	input.mi.mouseData = 0;
	input.mi.dwFlags = bDown ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
	input.mi.time = 0;
	input.mi.dwExtraInfo = 0;

	SendInput(1, &input, sizeof(INPUT));
}

void SetForeground(HWND hWnd)
{
	SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
	SetWindowPos(hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
	SetForegroundWindow(hWnd);
	SetFocus(hWnd);
}

int GetTierMap(const cv::Mat &fn_t1, const cv::Mat &fn_t2, const cv::Mat &fn_t3, const cv::Mat &fn_t4)
{
	int currentTier = 0;
	bool bBreak = false;

	if (!DIBSnapshot(GetDesktopWindow(), 100, [&fn_t1, &fn_t2, &fn_t3, &fn_t4, &bBreak, &currentTier](void *pBuffer, size_t cbBuffer, int width, int height, int bbp) {

		cv::Mat snap;

		if (!DIBToCvMat(snap, pBuffer, cbBuffer, width, height, bbp))
		{
			bBreak = true;
			OutputDebugStringA("Failed to DIBToCvMat");
			return;
		}

		cv::Mat found(width, height, CV_32FC1);
		cv::matchTemplate(snap, fn_t1, found, cv::TM_CCOEFF_NORMED);

		double minVal = -1;
		double maxVal = 0;
		cv::Point minLoc;
		cv::Point maxLoc;
		cv::Point matchLoc;

		cv::minMaxLoc(found, &minVal, &maxVal, &minLoc, &maxLoc, cv::Mat());

		if (maxVal < 0.96)
		{
			cv::matchTemplate(snap, fn_t2, found, cv::TM_CCOEFF_NORMED);

			double minVal = -1;
			double maxVal = 0;
			cv::Point minLoc;
			cv::Point maxLoc;
			cv::Point matchLoc;

			cv::minMaxLoc(found, &minVal, &maxVal, &minLoc, &maxLoc, cv::Mat());
			if (maxVal < 0.96)
			{
				cv::matchTemplate(snap, fn_t3, found, cv::TM_CCOEFF_NORMED);

				double minVal = -1;
				double maxVal = 0;
				cv::Point minLoc;
				cv::Point maxLoc;
				cv::Point matchLoc;

				cv::minMaxLoc(found, &minVal, &maxVal, &minLoc, &maxLoc, cv::Mat());
				if (maxVal < 0.96)
				{
					cv::matchTemplate(snap, fn_t4, found, cv::TM_CCOEFF_NORMED);

					double minVal = -1;
					double maxVal = 0;
					cv::Point minLoc;
					cv::Point maxLoc;
					cv::Point matchLoc;

					cv::minMaxLoc(found, &minVal, &maxVal, &minLoc, &maxLoc, cv::Mat());
					if (maxVal < 0.96)
					{

					}
					else
					{
						currentTier = 4;
						bBreak = true;
					}
				}
				else
				{
					currentTier = 3;
					bBreak = true;
				}
			}
			else
			{
				currentTier = 2;
				bBreak = true;
			}
		}
		else
		{
			currentTier = 1;
			bBreak = true;
		}
	}))
	{
		OutputDebugStringA("Failed to DIBSnapshot");
		return 0;
	}

	return currentTier;
}

bool CheckException(ULONG64 lastTick)
{
	HWND hwndTPWarning = FindWindowW(L"#32770", L"警告码 (3, 1015, 91001)");
	if (hwndTPWarning)
	{
		system("taskkill /f /im FortniteClient-Win64-Shipping.exe");		
		return true;
	}

	if (GetTickCount64() - lastTick > 1000 * 120)
	{
		system("taskkill /f /im FortniteClient-Win64-Shipping.exe");
		return true;
	}

	return false;
}

void GetLama()
{
	auto fn_saveworld = cv::imread("fn_saveworld.png");

	if (!fn_saveworld.size)
	{
		OutputDebugStringA("Failed to load fn_saveworld.png");
		return;
	}

	auto fn_abandon = cv::imread("fn_abandon.png");

	if (!fn_abandon.size)
	{
		OutputDebugStringA("Failed to load fn_abandon.png");
		return;
	}

	auto fn_openbtn = cv::imread("fn_openbtn.png");

	if (!fn_openbtn.size)
	{
		OutputDebugStringA("Failed to load fn_openbtn.png");
		return;
	}

	auto fn_nextbtn = cv::imread("fn_nextbtn.png");

	if (!fn_nextbtn.size)
	{
		OutputDebugStringA("Failed to load fn_nextbtn.png");
		return;
	}

	auto fn_news = cv::imread("fn_news.png");

	if (!fn_news.size)
	{
		OutputDebugStringA("Failed to load fn_news.png");
		return;
	}

	auto fn_power = cv::imread("fn_power.png");

	if (!fn_power.size)
	{
		OutputDebugStringA("Failed to load fn_power.png");
		return;
	}

	auto fn_start = cv::imread("fn_start.png");

	if (!fn_start.size)
	{
		OutputDebugStringA("Failed to load fn_start.png");
		return;
	}

	auto fn_team = cv::imread("fn_team.png");

	if (!fn_team.size)
	{
		OutputDebugStringA("Failed to load fn_team.png");
		return;
	}

	auto fn_t1 = cv::imread("fn_t1.png");

	if (!fn_t1.size)
	{
		OutputDebugStringA("Failed to load fn_t1.png");
		return;
	}

	auto fn_t2 = cv::imread("fn_t2.png");

	if (!fn_t2.size)
	{
		OutputDebugStringA("Failed to load fn_t2.png");
		return;
	}

	auto fn_t3 = cv::imread("fn_t3.png");

	if (!fn_t3.size)
	{
		OutputDebugStringA("Failed to load fn_t3.png");
		return;
	}

	auto fn_t4 = cv::imread("fn_t4.png");

	if (!fn_t4.size)
	{
		OutputDebugStringA("Failed to load fn_t4.png");
		return;
	}

	auto hwndWeGame = FindWindowW(L"TWINCONTROL", L"WeGame");

	if (!hwndWeGame)
	{
		OutputDebugStringA("Failed to find WeGame window");
		return;
	}

	auto lastTick64 = GetTickCount64();

	if (CheckException(lastTick64))
		return;

	int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
	int top = GetSystemMetrics(SM_YVIRTUALSCREEN);

	HWND hwndFn = FindWindowW(NULL, L"Fortnite  ");

	if (hwndFn)
	{
		//system("taskkill /f /im FortniteClient-Win64-Shipping.exe");
		goto gameStarted;
	}

	HWND hwndWeGameAd = FindWindowW(NULL, L"通用退弹窗口");

	if (hwndWeGameAd)
	{
		PostMessageW(hwndWeGameAd, WM_CLOSE, 0, 0);
	}

	SetForeground(hwndWeGame);

	Sleep(500);

	hwndWeGameAd = FindWindowW(NULL, L"通用退弹窗口");

	if (hwndWeGameAd)
	{
		PostMessageW(hwndWeGameAd, WM_CLOSE, 0, 0);
	}

	RECT rectWeGame;
	GetWindowRect(hwndWeGame, &rectWeGame);
	SetCursorPos(rectWeGame.right - 32, rectWeGame.bottom - 32);
	Sleep(100);

	hwndWeGameAd = FindWindowW(NULL, L"通用退弹窗口");

	if (hwndWeGameAd)
	{
		PostMessageW(hwndWeGameAd, WM_CLOSE, 0, 0);
	}

	SimMouseClick(TRUE);
	Sleep(50);
	SimMouseClick(FALSE);

	lastTick64 = GetTickCount64();

	while (1)
	{
		hwndFn = FindWindowW(NULL, L"Fortnite  ");

		if (hwndFn)
			break;

		if (CheckException(lastTick64))
			return;

		Sleep(1000);
	}

gameStarted:

	SetForeground(hwndFn);

	Sleep(1000);

	lastTick64 = GetTickCount64();

	while (1)
	{
		bool bBreak = false;

		if (!DIBSnapshot(GetDesktopWindow(), 100, [&fn_saveworld, &fn_abandon, &bBreak, left, top](void *pBuffer, size_t cbBuffer, int width, int height, int bbp) {

			cv::Mat snap;

			if (!DIBToCvMat(snap, pBuffer, cbBuffer, width, height, bbp))
			{
				bBreak = true;
				OutputDebugStringA("Failed to DIBToCvMat");
				return;
			}

			cv::Mat found(width, height, CV_32FC1);
			cv::matchTemplate(snap, fn_saveworld, found, cv::TM_CCOEFF_NORMED);

			double minVal = -1;
			double maxVal = 0;
			cv::Point minLoc;
			cv::Point maxLoc;
			cv::Point matchLoc;

			cv::minMaxLoc(found, &minVal, &maxVal, &minLoc, &maxLoc, cv::Mat());

			if (maxVal < 0.96)
			{
				cv::matchTemplate(snap, fn_abandon, found, cv::TM_CCOEFF_NORMED);

				double minVal = -1;
				double maxVal = 0;
				cv::Point minLoc;
				cv::Point maxLoc;
				cv::Point matchLoc;

				cv::minMaxLoc(found, &minVal, &maxVal, &minLoc, &maxLoc, cv::Mat());

				if (maxVal < 0.96)
				{

				}
				else
				{
					//cv::rectangle(snap, cv::Rect(minLoc.x, minLoc.y, fn_saveworld.cols, fn_saveworld.rows), cv::Scalar(255, 0, 0), 2, 8);
					//cv::imshow("123", snap);
					//cv::waitKey();
					OutputDebugStringA("found fn_abandon");
					SetCursorPos(left + maxLoc.x + fn_abandon.cols / 2, top + maxLoc.y + fn_abandon.rows / 2);
					Sleep(100);
					SimMouseClick(TRUE);
					Sleep(100);
					SimMouseClick(FALSE);
					Sleep(1500);
					SimMouseClick(TRUE);
					Sleep(100);
					SimMouseClick(FALSE);
					bBreak = true;
				}
			}

			else
			{
				//cv::rectangle(snap, cv::Rect(minLoc.x, minLoc.y, fn_saveworld.cols, fn_saveworld.rows), cv::Scalar(255, 0, 0), 2, 8);
				//cv::imshow("123", snap);
				//cv::waitKey();
				OutputDebugStringA("found fn_saveworld");
				SetCursorPos(left + maxLoc.x + fn_saveworld.cols / 2, top + maxLoc.y + fn_saveworld.rows / 2);
				Sleep(100);
				SimMouseClick(TRUE);
				Sleep(100);
				SimMouseClick(FALSE);
				Sleep(1500);
				SimMouseClick(TRUE);
				Sleep(100);
				SimMouseClick(FALSE);
				bBreak = true;
			}
		}))
		{
			OutputDebugStringA("Failed to DIBSnapshot");
			return;
		}

		if (bBreak)
			break;

		if (CheckException(lastTick64))
			return;

		Sleep(1000);
	}

	Sleep(3000);

	lastTick64 = GetTickCount64();

	while (1)
	{
		bool bBreak = false;

		if (!DIBSnapshot(GetDesktopWindow(), 100, [&fn_openbtn, &fn_news, &fn_power, &bBreak, left, top](void *pBuffer, size_t cbBuffer, int width, int height, int bbp) {

			cv::Mat snap;

			if (!DIBToCvMat(snap, pBuffer, cbBuffer, width, height, bbp))
			{
				bBreak = true;
				OutputDebugStringA("Failed to DIBToCvMat");
				return;
			}

			cv::Mat found(width, height, CV_32FC1);
			cv::matchTemplate(snap, fn_openbtn, found, cv::TM_CCOEFF_NORMED);

			double minVal = -1;
			double maxVal = 0;
			cv::Point minLoc;
			cv::Point maxLoc;
			cv::Point matchLoc;

			cv::minMaxLoc(found, &minVal, &maxVal, &minLoc, &maxLoc, cv::Mat());
			if (maxVal < 0.96)
			{
				cv::matchTemplate(snap, fn_news, found, cv::TM_CCOEFF_NORMED);

				double minVal = -1;
				double maxVal = 0;
				cv::Point minLoc;
				cv::Point maxLoc;
				cv::Point matchLoc;

				cv::minMaxLoc(found, &minVal, &maxVal, &minLoc, &maxLoc, cv::Mat());

				if (maxVal < 0.96)
				{
					cv::matchTemplate(snap, fn_power, found, cv::TM_CCOEFF_NORMED);

					double minVal = -1;
					double maxVal = 0;
					cv::Point minLoc;
					cv::Point maxLoc;
					cv::Point matchLoc;

					cv::minMaxLoc(found, &minVal, &maxVal, &minLoc, &maxLoc, cv::Mat());

					if (maxVal < 0.96)
					{

					}
					else
					{
						//cv::rectangle(snap, cv::Rect(minLoc.x, minLoc.y, fn_power.cols, fn_power.rows), cv::Scalar(255, 0, 0), 2, 8);
						//cv::imshow("123", snap);
						//cv::waitKey();

						OutputDebugStringA("found fn_power");

						SetCursorPos(left + 777, top + 106);
						Sleep(100);
						SimMouseClick(TRUE);
						Sleep(100);
						SimMouseClick(FALSE);
						Sleep(100);
						SimMouseClick(TRUE);
						Sleep(100);
						SimMouseClick(FALSE);
						bBreak = true;
					}
				}
				else
				{
					//cv::rectangle(snap, cv::Rect(minLoc.x, minLoc.y, fn_news.cols, fn_news.rows), cv::Scalar(255, 0, 0), 2, 8);
					//cv::imshow("123", snap);
					//cv::waitKey();

					OutputDebugStringA("found fn_news");

					SimKeyClick(VK_ESCAPE, TRUE);
					SimKeyClick(VK_ESCAPE, FALSE);
				}
			}
			else
			{
				//cv::rectangle(snap, cv::Rect(minLoc.x, minLoc.y, fn_openbtn.cols, fn_openbtn.rows), cv::Scalar(255, 0, 0), 2, 8);
				//cv::imshow("666", snap);
				//cv::waitKey();
				OutputDebugStringA("found fn_openbtn");
				SetCursorPos(left + maxLoc.x + fn_openbtn.cols / 2, top + maxLoc.y + fn_openbtn.rows / 2);
				for (int i = 0; i < 6; ++i)
				{
					SimMouseClick(TRUE);
					Sleep(100);
					SimMouseClick(FALSE);
					Sleep(1000);
				}
			}
		}))
		{
			OutputDebugStringA("Failed to DIBSnapshot");
			return;
		}

		if (bBreak)
			break;

		if (CheckException(lastTick64))
			return;

		Sleep(1000);
	}

	Sleep(1500);

	while (1)
	{
		auto tier = GetTierMap(fn_t1, fn_t2, fn_t3, fn_t4);
		if (tier >= 1 && tier < 4)
		{
			SetCursorPos(left + 150, top + 260);
			Sleep(50);
			SimMouseClick(TRUE);
			Sleep(50);
			SimMouseClick(FALSE);
			Sleep(50);
		}
		else if(tier == 4)
		{
			SetCursorPos(left + 1600, top + 900);
			Sleep(50);
			SimMouseClick(TRUE);
			Sleep(50);
			SimMouseClick(FALSE);
			Sleep(50);
			break;
		}
		else
		{
			Sleep(300);
		}
	}

	Sleep(3000);

	SetCursorPos(left + 1600, top + 900);
	Sleep(100);
	SimMouseClick(TRUE);
	Sleep(50);
	SimMouseClick(FALSE);
	Sleep(50);

	SetCursorPos(left + 500, top + 500);

	Sleep(3000);

	lastTick64 = GetTickCount64();

	while (1)
	{
		bool bBreak = false;

		if (!DIBSnapshot(GetDesktopWindow(), 100, [&fn_start, &bBreak, left, top](void *pBuffer, size_t cbBuffer, int width, int height, int bbp) {

			cv::Mat snap;

			if (!DIBToCvMat(snap, pBuffer, cbBuffer, width, height, bbp))
			{
				bBreak = true;
				OutputDebugStringA("Failed to DIBToCvMat");
				return;
			}

			cv::Mat found(width, height, CV_32FC1);
			cv::matchTemplate(snap, fn_start, found, cv::TM_CCOEFF_NORMED);

			double minVal = -1;
			double maxVal = 0;
			cv::Point minLoc;
			cv::Point maxLoc;
			cv::Point matchLoc;

			cv::minMaxLoc(found, &minVal, &maxVal, &minLoc, &maxLoc, cv::Mat());

			if (maxVal < 0.96)
			{
				return;
			}

			//cv::rectangle(snap, cv::Rect(minLoc.x, minLoc.y, fn_start.cols, fn_start.rows), cv::Scalar(255, 0, 0), 2, 8);
			//cv::imshow("123", snap);
			//cv::waitKey();

			OutputDebugStringA("found fn_start");

			SetCursorPos(left + maxLoc.x + fn_start.cols / 2, top + maxLoc.y + fn_start.rows / 2);
			Sleep(100);
			SimMouseClick(TRUE);
			SimMouseClick(FALSE);
			Sleep(100);
			SimMouseClick(TRUE);
			SimMouseClick(FALSE);
			bBreak = true;
		}))
		{
			OutputDebugStringA("Failed to DIBSnapshot");
			return;
		}

		if (bBreak)
			break;

		if (CheckException(lastTick64))
			return;

		Sleep(1000);
	}

	Sleep(3000);

	lastTick64 = GetTickCount64();

	while (1)
	{
		bool bBreak = false;

		if (!DIBSnapshot(GetDesktopWindow(), 100, [&fn_team, &bBreak](void *pBuffer, size_t cbBuffer, int width, int height, int bbp) {

			cv::Mat snap;

			if (!DIBToCvMat(snap, pBuffer, cbBuffer, width, height, bbp))
			{
				bBreak = true;
				OutputDebugStringA("Failed to DIBToCvMat");
				return;
			}

			cv::Mat found(width, height, CV_32FC1);
			cv::matchTemplate(snap, fn_team, found, cv::TM_CCOEFF_NORMED);

			double minVal = -1;
			double maxVal = 0;
			cv::Point minLoc;
			cv::Point maxLoc;
			cv::Point matchLoc;

			cv::minMaxLoc(found, &minVal, &maxVal, &minLoc, &maxLoc, cv::Mat());

			if (maxVal < 0.96)
			{
				return;
			}

			//cv::rectangle(snap, cv::Rect(minLoc.x, minLoc.y, fn_start.cols, fn_start.rows), cv::Scalar(255, 0, 0), 2, 8);
			//cv::imshow("123", snap);
			//cv::waitKey();

			OutputDebugStringA("found fn_team");
			bBreak = true;
		}))
		{
			OutputDebugStringA("Failed to DIBSnapshot");
			return;
		}

		if (bBreak)
			break;

		if (CheckException(lastTick64))
			return;

		Sleep(1000);
	}

	SimKeyClick(VK_ESCAPE, TRUE);
	SimKeyClick(VK_ESCAPE, FALSE);

	Sleep(500);

	SetCursorPos(left + 1600, top + 406);

	Sleep(200);

	SimMouseClick(TRUE);
	Sleep(100);
	SimMouseClick(FALSE);

	Sleep(500);

	SetCursorPos(left + 1200, top + 755);

	Sleep(200);

	SimMouseClick(TRUE);
	Sleep(100);
	SimMouseClick(FALSE);

	Sleep(1500);

	lastTick64 = GetTickCount64();

	if (CheckException(lastTick64))
		return;

	SimKeyClick(VK_MENU, TRUE);
	Sleep(100);
	SimKeyClick(VK_F4, TRUE);
	SimKeyClick(VK_F4, FALSE);
	Sleep(100);
	SimKeyClick(VK_MENU, FALSE);
	//system("taskkill /f /im FortniteClient-Win64-Shipping.exe");

	Sleep(1000);

	lastTick64 = GetTickCount64();

	while (1)
	{
		hwndWeGameAd = FindWindowW(NULL, L"通用退弹窗口");

		if (hwndWeGameAd)
		{
			PostMessageW(hwndWeGameAd, WM_CLOSE, 0, 0);
			break;
		}

		if (CheckException(lastTick64))
			return;

		Sleep(1000);
	}
}

void OpenLama()
{
	int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
	int top = GetSystemMetrics(SM_YVIRTUALSCREEN);

	auto fn_minilama = cv::imread("fn_minilama.png");

	if (!fn_minilama.size)
	{
		OutputDebugStringA("Failed to load fn_minilama.png");
		return;
	}

	auto fn_continuebtn = cv::imread("fn_continuebtn.png");

	if (!fn_continuebtn.size)
	{
		OutputDebugStringA("Failed to load fn_continuebtn.png");
		return;
	}

	auto fn_back = cv::imread("fn_back.png");

	if (!fn_back.size)
	{
		OutputDebugStringA("Failed to load fn_back.png");
		return;
	}

	auto fn_attack = cv::imread("fn_attack.png");

	if (!fn_attack.size)
	{
		OutputDebugStringA("Failed to load fn_attack.png");
		return;
	}

	HWND hwndFn = FindWindowW(NULL, L"Fortnite  ");

	if (!hwndFn)
	{
		OutputDebugStringA("Fortnite window not found");
		return;
	}

	SetForeground(hwndFn);

	bool bAttack = false;

	Sleep(1000);

	while (1)
	{
		bool bBreak = false;

		if (!DIBSnapshot(GetDesktopWindow(), 100, [&fn_attack, &fn_minilama, &bBreak, &bAttack, left, top](void *pBuffer, size_t cbBuffer, int width, int height, int bbp) {

			cv::Mat snap;

			if (!DIBToCvMat(snap, pBuffer, cbBuffer, width, height, bbp))
			{
				bBreak = true;
				OutputDebugStringA("Failed to DIBToCvMat");
				return;
			}

			cv::Mat found(width, height, CV_32FC1);
			cv::matchTemplate(snap, fn_minilama, found, cv::TM_CCOEFF_NORMED);

			double minVal = -1;
			double maxVal = 0;
			cv::Point minLoc;
			cv::Point maxLoc;
			cv::Point matchLoc;

			cv::minMaxLoc(found, &minVal, &maxVal, &minLoc, &maxLoc, cv::Mat());

			if (maxVal < 0.96)
			{
				cv::matchTemplate(snap, fn_attack, found, cv::TM_CCOEFF_NORMED);

				double minVal = -1;
				double maxVal = 0;
				cv::Point minLoc;
				cv::Point maxLoc;
				cv::Point matchLoc;

				cv::minMaxLoc(found, &minVal, &maxVal, &minLoc, &maxLoc, cv::Mat());

				if (maxVal < 0.96)
				{
					return;
				}

				//cv::rectangle(snap, cv::Rect(minLoc.x, minLoc.y, fn_minilama.cols, fn_minilama.rows), cv::Scalar(255, 0, 0), 2, 8);
				//cv::imshow("123", snap);
				//cv::waitKey();

				OutputDebugStringA("found fn_attack");

				bAttack = true;
				return;
			}
			else
			{
				//cv::rectangle(snap, cv::Rect(minLoc.x, minLoc.y, fn_minilama.cols, fn_minilama.rows), cv::Scalar(255, 0, 0), 2, 8);
				//cv::imshow("123", snap);
				//cv::waitKey();

				OutputDebugStringA("found fn_minilama");

				SetCursorPos(left + maxLoc.x + fn_minilama.cols / 2, top + maxLoc.y + fn_minilama.rows / 2 + 135);
				Sleep(100);
				SetCursorPos(left + maxLoc.x + fn_minilama.cols / 2, top + maxLoc.y + fn_minilama.rows / 2 + 140);
				Sleep(1000);
				SimMouseClick(TRUE);
				Sleep(50);
				SimMouseClick(FALSE);
				Sleep(100);
				SimMouseClick(TRUE);
				Sleep(50);
				SimMouseClick(FALSE);
				Sleep(1000);
				bBreak = true;
				return;
			}
		}))
		{
			OutputDebugStringA("Failed to DIBSnapshot");
			return;
		}

		if (bBreak || bAttack)
			break;

		Sleep(1000);
	}

	if (bAttack)
		goto attackstage;

	while (1)
	{
		bool bBreak = false;

		if (!DIBSnapshot(GetDesktopWindow(), 100, [&fn_continuebtn, &bBreak, left, top](void *pBuffer, size_t cbBuffer, int width, int height, int bbp) {

			cv::Mat snap;

			if (!DIBToCvMat(snap, pBuffer, cbBuffer, width, height, bbp))
			{
				bBreak = true;
				OutputDebugStringA("Failed to DIBToCvMat");
				return;
			}

			cv::Mat found(width, height, CV_32FC1);
			cv::matchTemplate(snap, fn_continuebtn, found, cv::TM_CCOEFF_NORMED);

			double minVal = -1;
			double maxVal = 0;
			cv::Point minLoc;
			cv::Point maxLoc;
			cv::Point matchLoc;

			cv::minMaxLoc(found, &minVal, &maxVal, &minLoc, &maxLoc, cv::Mat());

			if (maxVal < 0.96)
			{
				return;
			}

			//cv::rectangle(snap, cv::Rect(minLoc.x, minLoc.y, fn_minilama.cols, fn_minilama.rows), cv::Scalar(255, 0, 0), 2, 8);
			//cv::imshow("123", snap);
			//cv::waitKey();

			OutputDebugStringA("found fn_continuebtn");

			SetCursorPos(left + maxLoc.x + fn_continuebtn.cols / 2, top + maxLoc.y + fn_continuebtn.rows / 2);
			SimMouseClick(TRUE);
			SimMouseClick(FALSE);
			bBreak = true;
		}))
		{
			OutputDebugStringA("Failed to DIBSnapshot");
			return;
		}

		if (bBreak)
			break;

		Sleep(1000);
	}

	Sleep(1000);

attackstage:

	SetCursorPos(left + 400, top + 300);

	bool bLastClicking = false;
	while (1)
	{
		bool bOpening = false;
		bool bBreak = false;
		if (!bLastClicking)
		{
			SimMouseClick(TRUE);
			bLastClicking = true;
		}

		if (!DIBSnapshot(GetDesktopWindow(), 100, [&bOpening, &bLastClicking, &bBreak, &fn_attack, &fn_back, left, top](void *pBuffer, size_t cbBuffer, int width, int height, int bbp) {

			cv::Mat snap;

			if (!DIBToCvMat(snap, pBuffer, cbBuffer, width, height, bbp))
			{
				OutputDebugStringA("Failed to DIBToCvMat");
				return;
			}

			cv::Mat found(width, height, CV_32FC1);
			cv::matchTemplate(snap, fn_back, found, cv::TM_CCOEFF_NORMED);

			double minVal = -1;
			double maxVal = 0;
			cv::Point minLoc;
			cv::Point maxLoc;
			cv::Point matchLoc;

			cv::minMaxLoc(found, &minVal, &maxVal, &minLoc, &maxLoc, cv::Mat());

			if (maxVal < 0.96)
			{
				cv::matchTemplate(snap, fn_attack, found, cv::TM_CCOEFF_NORMED);

				double minVal = -1;
				double maxVal = 0;
				cv::Point minLoc;
				cv::Point maxLoc;
				cv::Point matchLoc;

				cv::minMaxLoc(found, &minVal, &maxVal, &minLoc, &maxLoc, cv::Mat());

				if (maxVal < 0.96)
				{
					bOpening = true;
					return;
				}

				//cv::rectangle(snap, cv::Rect(minLoc.x, minLoc.y, fn_minilama.cols, fn_minilama.rows), cv::Scalar(255, 0, 0), 2, 8);
				//cv::imshow("123", snap);
				//cv::waitKey();

				OutputDebugStringA("found fn_attack");

				SetCursorPos(left + 400, top + 300);
				SimMouseClick(TRUE);
				Sleep(100);
				SimMouseClick(FALSE);
				return;
			}
			else
			{
				//cv::rectangle(snap, cv::Rect(minLoc.x, minLoc.y, fn_minilama.cols, fn_minilama.rows), cv::Scalar(255, 0, 0), 2, 8);
				//cv::imshow("123", snap);
				//cv::waitKey();

				OutputDebugStringA("found fn_back");

				SimKeyClick(VK_ESCAPE, TRUE);
				SimKeyClick(VK_ESCAPE, FALSE);

				SetCursorPos(left + 400, top + 300);
				bBreak = true;
			}
		}))
		{
			OutputDebugStringA("Failed to DIBSnapshot");
			return;
		}

		if (bBreak)
			break;

		if (!bOpening)
		{
			if (bLastClicking)
			{
				SimMouseClick(FALSE);
				bLastClicking = false;
			}
			Sleep(1000);
		}
	}
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR szCmdLine, int iCmdShow) {

	if (strstr(szCmdLine, "-get"))
		GetLama();
	else if (strstr(szCmdLine, "-open"))
		OpenLama();

	return 0;
}