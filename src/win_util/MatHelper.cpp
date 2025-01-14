﻿#include "pch.h"

#include "gtl/mat_helper.h"
#include "gtl/win_util/MatHelper.h"
#include "gtl/win_util/ProgressDlg.h"
#include "spdlog/spdlog.h"
#include "spdlog/stopwatch.h"

namespace gtl::win_util {

	bool MatToDC(cv::Mat const& img, cv::Size const& sizeEffective, CDC& dc, CRect const& rectTarget, std::span<RGBQUAD> palette) {

		auto const type = img.type();

		int pixel_size = (type == CV_8UC3) ? 3 : ((type == CV_8UC1) ? 1 : 0);
		if (pixel_size <= 0)
			return false;

		uint8_t const* pImage = nullptr;
		int cx = gtl::AdjustAlign32(img.cols);
		int cy = img.rows;

		try {

			if (cx*pixel_size != img.step) {
				thread_local static std::vector<BYTE> buf;
				int nBufferPitch = cx *pixel_size;
				int row_size = img.cols * pixel_size;
				auto size = nBufferPitch * img.rows;
				if (buf.size() != size) {
					buf.assign(size, 0);
				}
				pImage = buf.data();
				auto * pos = buf.data();
				for (int y = 0; y < img.rows; y++) {
					memcpy(pos, img.ptr(y), row_size);
					pos += nBufferPitch;
				}
			} else {
				pImage = img.ptr<BYTE>(0);
			}

			struct BMP {
				bool bPaletteInitialized{false};
				BITMAPINFO bmpInfo{};
				RGBQUAD dummy[256-1]{};	// starting from bmpInfo
			};
			thread_local static BMP bmp;
			std::optional<BMP> rBMP_Local;
			BMP* pBMP = &bmp;
			if (pixel_size == 1) {

				if (!palette.empty()) {
					rBMP_Local.emplace();
					auto& b = rBMP_Local.value();
					auto n = std::min(std::size(b.dummy)+1, palette.size());
					for (size_t i{}; i < n; i++) {
						b.bmpInfo.bmiColors[i] = palette[i];
					}
					b.bPaletteInitialized = n > 0;
					pBMP = &(*rBMP_Local);
				}

				if (!pBMP->bPaletteInitialized) {
					pBMP->bPaletteInitialized = true;
					auto n = std::size(pBMP->dummy)+1;
					for (size_t i {}; i < n; i++) {
						(gtl::color_bgra_t&)pBMP->bmpInfo.bmiColors[i] = ColorBGRA((uint8_t)i, (uint8_t)i, (uint8_t)i);
					}
				}

			}
			BITMAPINFO& bmpInfo = pBMP->bmpInfo;
			bmpInfo.bmiHeader.biSize = sizeof(bmpInfo.bmiHeader);
			bmpInfo.bmiHeader.biWidth = cx;
			bmpInfo.bmiHeader.biHeight = -cy;
			bmpInfo.bmiHeader.biPlanes = 1;
			bmpInfo.bmiHeader.biBitCount = 8*pixel_size;
			bmpInfo.bmiHeader.biCompression = BI_RGB;
			bmpInfo.bmiHeader.biSizeImage = cx * cy * pixel_size;
			bmpInfo.bmiHeader.biXPelsPerMeter = 0;
			bmpInfo.bmiHeader.biYPelsPerMeter = 0;
			bmpInfo.bmiHeader.biClrUsed = pixel_size == 1 ? 256 : 0;
			bmpInfo.bmiHeader.biClrImportant = pixel_size == 1 ? 256 : 0;

			CRect rectSrc(0, 0, sizeEffective.width, sizeEffective.height);
			CRect rectDst(rectTarget);
			CalcViewPosition(sizeEffective, rectTarget, rectSrc, rectDst);
			SetDIBitsToDevice(dc,
								rectDst.left, rectDst.top, rectDst.Width(), rectDst.Height(),
								rectSrc.left, rectSrc.top, 0, img.rows, pImage, &bmpInfo, /*pixel_size == 1 ? DIB_PAL_COLORS : */DIB_RGB_COLORS);

			pImage = nullptr;
		} catch (...) {
			TRACE(_T(__FUNCTION__) _T(" - Unknown Error..."));
			return false;
		}

		return true;
	}

	bool MatToDCTransparent(cv::Mat const& img, cv::Size const& sizeView, CDC& dc, CRect const& rect, COLORREF crTransparent) {
		if (img.empty() || !dc.m_hDC || rect.IsRectEmpty())
			return false;

		CDC dcMem;
		dcMem.CreateCompatibleDC(&dc);
		CBitmap bmp;
		bmp.CreateCompatibleBitmap(&dc, sizeView.width, sizeView.height);
		dcMem.SelectObject(&bmp);
		CBrush brush(crTransparent);
		dcMem.FillRect(rect, &brush);
		MatToDC(img, sizeView, dcMem, CRect(0, 0, sizeView.width, sizeView.height));

		CRect rectSrc, rectDst;
		CalcViewPosition(sizeView, rect, rectSrc, rectDst);

		return dc.TransparentBlt(rectDst.left, rectDst.top, rectDst.Width(), rectDst.Height(), &dcMem, rectSrc.left, rectSrc.top, rectSrc.Width(), rectSrc.Height(), crTransparent) != FALSE;
	}

	bool MatToDCAlphaBlend(cv::Mat const& img, cv::Size const& sizeView, CDC& dc, CRect const& rect, BLENDFUNCTION blend) {
		if (img.empty() || !dc.m_hDC || rect.IsRectEmpty())
			return FALSE;

		CDC dcMem;
		dcMem.CreateCompatibleDC(&dc);
		CBitmap bmp;
		bmp.CreateCompatibleBitmap(&dc, sizeView.width, sizeView.height);
		dcMem.SelectObject(&bmp);
		//CBrush brush(crTransparent);
		//dcMem.FillRect(rect, &brush);
		MatToDC(img, sizeView, dcMem, CRect(0, 0, sizeView.width, sizeView.height));

		CRect rectSrc, rectDst;
		CalcViewPosition(sizeView, rect, rectSrc, rectDst);

		return dc.AlphaBlend(rectDst.left, rectDst.top, rectDst.Width(), rectDst.Height(), &dcMem, rectSrc.left, rectSrc.top, rectSrc.Width(), rectSrc.Height(), blend) != FALSE;
	}

	bool SaveBitmapMatProgress(std::filesystem::path const& path, cv::Mat const& img, int nBPP, gtl::xSize2i const& pelsPerMeter, std::span<gtl::color_bgra_t> palette, bool bNoPaletteLookup, bool bBottom2Top) {
		CProgressDlg dlgProgress;
		dlgProgress.m_strMessage.Format(_T("Saving : %s"), path.wstring().c_str());

		dlgProgress.m_rThreadWorker = std::make_unique<std::jthread>(gtl::SaveBitmapMat, path, img, nBPP, pelsPerMeter, palette, bNoPaletteLookup, bBottom2Top, dlgProgress.m_calback);

		auto r = dlgProgress.DoModal();

		CWaitCursor wc;

		dlgProgress.m_rThreadWorker->join();

		bool bResult = (r == IDOK);
		if (!bResult)
			std::filesystem::remove(path);

		return bResult;
	};

	cv::Mat LoadBitmapMatProgress(std::filesystem::path const& path, gtl::xSize2i& pelsPerMeter) {
		cv::Mat img;
		CProgressDlg dlgProgress;
		dlgProgress.m_strMessage.Format(_T("Loading : %s"), path.c_str());

		dlgProgress.m_rThreadWorker = std::make_unique<std::jthread>([&img, &path, &pelsPerMeter, &dlgProgress]() { img = gtl::LoadBitmapMat(path, pelsPerMeter, dlgProgress.m_calback); });
		auto r = dlgProgress.DoModal();

		CWaitCursor wc;

		dlgProgress.m_rThreadWorker->join();

		bool bResult = (r == IDOK);
		if (!bResult)
			img.release();

		return img;
	}

	cv::Mat LoadBitmapMatPixelArrayProgress(std::filesystem::path const& path, gtl::xSize2i& pelsPerMeter, std::vector<gtl::color_bgra_t>& palette) {
		cv::Mat img;
		CProgressDlg dlgProgress;
		dlgProgress.m_strMessage.Format(_T("Loading : %s"), path.c_str());

		dlgProgress.m_rThreadWorker = std::make_unique<std::jthread>([&img, &path, &pelsPerMeter, &palette, &dlgProgress]() { img = gtl::LoadBitmapMatPixelArray(path, pelsPerMeter, palette, dlgProgress.m_calback); });
		auto r = dlgProgress.DoModal();

		CWaitCursor wc;

		dlgProgress.m_rThreadWorker->join();

		bool bResult = (r == IDOK);
		if (!bResult)
			img.release();

		return img;
	}

	//=============================================================================
	//

}	// namespace gtl::win_util
