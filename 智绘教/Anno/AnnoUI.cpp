#include "AnnoCore.h"
#include <windowsx.h>
#include <set>
#include <algorithm>
#include <cstdlib>
#include <cstdio>

namespace anno
{
	HWND g_overlay = nullptr;
	static HWND g_hidden = nullptr;
	static HWND g_bar[2] = { nullptr, nullptr };
	static HWND g_popup = nullptr;

	static bool g_visible = true;
	static bool g_collapsed = false;
	static Tool g_tool = Tool::Select;
	static Kind g_shapeKind = Kind::Square;
	static bool g_dirty = true;

	// ---------------- 缩放 ----------------
	static float UiScale()
	{
		static float s = -1.0f;
		if (s > 0.0f) return s;
		HDC dc = GetDC(nullptr);
		int dpi = GetDeviceCaps(dc, LOGPIXELSX);
		ReleaseDC(nullptr, dc);
		s = dpi > 0 ? (float)dpi / 96.0f : 1.0f;
		if (s < 0.75f) s = 0.75f;
		if (s > 3.0f) s = 3.0f;
		return s;
	}

	// ---------------- 离屏画布 ----------------
	struct Canvas
	{
		HDC hdc = nullptr;
		HBITMAP bmp = nullptr;
		void* bits = nullptr;
		int w = 0, h = 0;

		bool Ensure(int W, int H)
		{
			if (W < 1) W = 1;
			if (H < 1) H = 1;
			if (hdc && w == W && h == H) return true;
			Destroy();
			HDC screen = GetDC(nullptr);
			hdc = CreateCompatibleDC(screen);
			BITMAPINFO bi{};
			bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			bi.bmiHeader.biWidth = W;
			bi.bmiHeader.biHeight = -H;
			bi.bmiHeader.biPlanes = 1;
			bi.bmiHeader.biBitCount = 32;
			bi.bmiHeader.biCompression = BI_RGB;
			void* bitsPtr = nullptr;
			bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bitsPtr, nullptr, 0);
			if (bmp) SelectObject(hdc, bmp);
			bits = bitsPtr;
			w = W;
			h = H;
			ReleaseDC(nullptr, screen);
			return hdc && bmp && bits;
		}
		void Destroy()
		{
			if (bmp) { DeleteObject(bmp); bmp = nullptr; }
			if (hdc) { DeleteDC(hdc); hdc = nullptr; }
			bits = nullptr;
			w = h = 0;
		}
	};

	static Canvas g_cvOverlay;
	static Canvas g_cvBar[2];
	static Canvas g_cvPopup;

	static void Present(HWND hwnd, Canvas& cv, int x, int y)
	{
		if (!cv.hdc || !cv.bits) return;
		HDC screen = GetDC(nullptr);
		POINT src{ 0, 0 };
		SIZE sz{ cv.w, cv.h };
		POINT dst{ x, y };
		BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
		UpdateLayeredWindow(hwnd, screen, &dst, &sz, cv.hdc, &src, 0, &bf, ULW_ALPHA);
		ReleaseDC(nullptr, screen);
	}

	// ---------------- 绘制基础 ----------------
	static void FillRound(Graphics& g, const RectF& r, float rad, const Color& fill, const Color& border, float bw = 1.0f)
	{
		GraphicsPath path;
		float d = rad * 2.0f;
		float x = r.X, y = r.Y, w = r.Width, h = r.Height;
		if (w < d) d = w;
		if (h < d) d = h;
		path.AddArc(x, y, d, d, 180.0f, 90.0f);
		path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
		path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
		path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
		path.CloseFigure();
		SolidBrush b(fill);
		g.FillPath(&b, &path);
		if (border.GetA() > 0)
		{
			Pen p(border, bw);
			g.DrawPath(&p, &path);
		}
	}

	static void DrawToolIcon(Graphics& g, int idx, const RectF& r, const Color& c)
	{
		float x = r.X, y = r.Y, w = r.Width, h = r.Height;
		float cx = x + w * 0.5f, cy = y + h * 0.5f;
		Pen pen(c, 2.2f);
		pen.SetLineJoin(LineJoinRound);
		pen.SetStartCap(LineCapRound);
		pen.SetEndCap(LineCapRound);
		SolidBrush br(c);

		switch (idx)
		{
		case 0: // 选择（鼠标指针）
		{
			PointF p[7] = {
				PointF(cx - w * 0.14f, cy - h * 0.28f),
				PointF(cx - w * 0.14f, cy + h * 0.16f),
				PointF(cx - w * 0.02f, cy + h * 0.06f),
				PointF(cx + w * 0.05f, cy + h * 0.28f),
				PointF(cx + w * 0.13f, cy + h * 0.24f),
				PointF(cx + w * 0.06f, cy + h * 0.03f),
				PointF(cx + w * 0.18f, cy - h * 0.01f)
			};
			g.FillPolygon(&br, p, 7);
			break;
		}
		case 1: // 笔
		{
			g.DrawLine(&pen, PointF(cx - w * 0.24f, cy + h * 0.24f), PointF(cx + w * 0.20f, cy - h * 0.20f));
			PointF p[3] = { PointF(cx + w * 0.24f, cy - h * 0.24f), PointF(cx + w * 0.10f, cy - h * 0.20f), PointF(cx + w * 0.20f, cy - h * 0.10f) };
			g.FillPolygon(&br, p, 3);
			g.FillEllipse(&br, cx - w * 0.28f, cy + h * 0.20f, w * 0.10f, h * 0.10f);
			break;
		}
		case 2: // 橡皮
		{
			GraphicsState st = g.Save();
			g.TranslateTransform(cx, cy);
			g.RotateTransform(-35.0f);
			RectF rr(-w * 0.26f, -h * 0.14f, w * 0.52f, h * 0.28f);
			Pen p2(c, 2.0f);
			g.DrawRectangle(&p2, rr);
			g.DrawLine(&p2, 0.0f, -h * 0.14f, 0.0f, h * 0.14f);
			g.Restore(st);
			break;
		}
		case 3: // 套索
		{
			Pen d(c, 2.0f);
			d.SetDashStyle(DashStyleDash);
			g.DrawEllipse(&d, x + w * 0.16f, y + h * 0.16f, w * 0.66f, h * 0.56f);
			g.DrawLine(&pen, PointF(cx + w * 0.12f, cy + h * 0.30f), PointF(cx - w * 0.06f, cy + h * 0.40f));
			break;
		}
		case 4: // 图形
			g.DrawRectangle(&pen, x + w * 0.12f, y + h * 0.16f, w * 0.46f, h * 0.46f);
			g.DrawEllipse(&pen, x + w * 0.38f, y + h * 0.36f, w * 0.48f, h * 0.48f);
			break;
		case 5: // 清屏
			g.DrawRectangle(&pen, x + w * 0.24f, y + h * 0.28f, w * 0.46f, h * 0.50f);
			g.DrawLine(&pen, PointF(x + w * 0.18f, y + h * 0.22f), PointF(x + w * 0.76f, y + h * 0.22f));
			g.DrawLine(&pen, PointF(x + w * 0.40f, y + h * 0.16f), PointF(x + w * 0.55f, y + h * 0.16f));
			break;
		case 6: // 收起
		{
			g.DrawLine(&pen, PointF(cx - w * 0.16f, cy - h * 0.06f), PointF(cx, cy + h * 0.12f));
			g.DrawLine(&pen, PointF(cx + w * 0.16f, cy - h * 0.06f), PointF(cx, cy + h * 0.12f));
			break;
		}
		default:
			break;
		}
	}

	// ---------------- 布局常量 ----------------
	static int BarPad() { return (int)(8.0f * UiScale()); }
	static int BarGap() { return (int)(5.0f * UiScale()); }
	static int BarBtn() { return (int)(38.0f * UiScale()); }
	static int BarW() { return (int)(60.0f * UiScale()); }
	static int BarH() { return BarPad() * 2 + BarBtn() * 7 + BarGap() * 6; }
	static int BarCollapsed() { return (int)(46.0f * UiScale()); }

	static RECT MonitorRect()
	{
		RECT rc{ 0, 0, 1920, 1080 };
		MONITORINFO mi{};
		mi.cbSize = sizeof(mi);
		HMONITOR mon = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
		if (GetMonitorInfoW(mon, &mi)) rc = mi.rcMonitor;
		return rc;
	}

	static void GetBarRect(int side, int& x, int& y, int& w, int& h)
	{
		RECT mon = MonitorRect();
		w = g_collapsed ? BarCollapsed() : BarW();
		h = g_collapsed ? BarCollapsed() : BarH();
		if (side == 0)
		{
			x = (cfg.leftX >= 0) ? cfg.leftX : mon.left + (int)(12.0f * UiScale());
			y = (cfg.leftY >= 0) ? cfg.leftY : mon.top + (int)((mon.bottom - mon.top) * 2.0f / 3.0f);
		}
		else
		{
			int rightEdge = (cfg.rightX >= 0) ? cfg.rightX : mon.right - (int)(12.0f * UiScale());
			x = rightEdge - w;
			y = (cfg.rightY >= 0) ? cfg.rightY : mon.top + (int)((mon.bottom - mon.top) * 2.0f / 3.0f);
		}
		// 夹到屏幕内
		if (x < mon.left) x = mon.left;
		if (y < mon.top) y = mon.top;
		if (x + w > mon.right) x = mon.right - w;
		if (y + h > mon.bottom) y = mon.bottom - h;
	}

	// ---------------- 交互状态 ----------------
	enum class DragMode { None, Pen, Erase, Lasso, ShapeCreate, Handle, MoveSel, Vertex };
	static DragMode g_drag = DragMode::None;
	static std::vector<PointF> g_penPts;
	static std::vector<PointF> g_lasso;
	static PointF g_p0{ 0, 0 };
	static PointF g_last{ 0, 0 };
	static std::unique_ptr<Obj> g_preview;
	static PointF g_erasePos{ -9999, -9999 };
	static bool g_eraseShow = false;
	static int g_eraseIdxActive = 1;
	static bool g_eraseSquareActive = false;

	static Obj* g_xformObj = nullptr;
	static int g_xformHandle = 0;
	static float g_xStartRot = 0.0f;
	static float g_xStartAngle = 0.0f;
	static float g_xStartDist = 1.0f;
	static std::vector<PointF> g_xStartLocal;

	static Obj* g_vertexObj = nullptr;
	static int g_vertexIdx = -1;

	// 触摸
	static std::set<UINT32> g_touchIds;
	static bool g_multiErase = false;
	static int g_multiEraseIdx = 1;
	static std::vector<PointF> g_multiErasePts;

	static int g_vx = 0, g_vy = 0, g_vw = 1920, g_vh = 1080;

	static PointF W(const PointF& client) { return PointF(client.X + (float)g_vx, client.Y + (float)g_vy); }

	// ---------------- 选择框 / 手柄 ----------------
	static float SelMargin() { return 14.0f * UiScale(); }
	static float HandleR() { return 8.0f * UiScale(); }
	static float VertexR() { return 5.5f * UiScale(); }
	static float DeleteR() { return 9.5f * UiScale(); }

	static void ObjHandleRects(const Obj& o, PointF out[4], RectF& box)
	{
		RectF b = ObjWorldBounds(o);
		float m = SelMargin();
		box = RectF(b.X - m, b.Y - m, b.Width + m * 2.0f, b.Height + m * 2.0f);
		out[0] = PointF(box.X, box.Y);
		out[1] = PointF(box.X + box.Width, box.Y);
		out[2] = PointF(box.X + box.Width, box.Y + box.Height);
		out[3] = PointF(box.X, box.Y + box.Height);
	}

	static bool HitSelectedHandle(const PointF& p, Obj*& obj, int& idx)
	{
		float r = HandleR() + 3.0f;
		for (auto it = doc.objs.rbegin(); it != doc.objs.rend(); ++it)
		{
			Obj& o = **it;
			if (!o.selected) continue;
			PointF h[4]; RectF box;
			ObjHandleRects(o, h, box);
			for (int i = 0; i < 4; i++)
			{
				if (PDist(p, h[i]) <= r) { obj = &o; idx = i; return true; }
			}
		}
		return false;
	}

	static bool HitSelectedVertex(const PointF& p, Obj*& obj, int& idx)
	{
		float r = VertexR() + 4.0f;
		for (auto it = doc.objs.rbegin(); it != doc.objs.rend(); ++it)
		{
			Obj& o = **it;
			if (!o.selected) continue;
			std::vector<PointF> v = ObjVertices(o);
			for (int i = 0; i < (int)v.size(); i++)
			{
				if (PDist(p, v[i]) <= r) { obj = &o; idx = i; return true; }
			}
		}
		return false;
	}

	static bool HitSelectedArea(const PointF& p)
	{
		float m = SelMargin();
		for (auto it = doc.objs.rbegin(); it != doc.objs.rend(); ++it)
		{
			Obj& o = **it;
			if (!o.selected) continue;
			RectF b = ObjWorldBounds(o);
			if (p.X >= b.X - m && p.X <= b.X + b.Width + m && p.Y >= b.Y - m && p.Y <= b.Y + b.Height + m) return true;
		}
		return false;
	}

	// 删除按钮：选中框右上角外侧
	static PointF DeletePos(const RectF& box)
	{
		float r = DeleteR();
		PointF c(box.X + box.Width + r + 4.0f, box.Y - r - 4.0f);
		float minX = (float)g_vx + r + 2.0f, maxX = (float)(g_vx + g_vw) - r - 2.0f;
		float minY = (float)g_vy + r + 2.0f, maxY = (float)(g_vy + g_vh) - r - 2.0f;
		if (c.X < minX) c.X = minX;
		if (c.X > maxX) c.X = maxX;
		if (c.Y < minY) c.Y = minY;
		if (c.Y > maxY) c.Y = maxY;
		return c;
	}

	static bool HitSelectedDelete(const PointF& p, Obj*& obj)
	{
		float r = DeleteR() + 4.0f;
		for (auto it = doc.objs.rbegin(); it != doc.objs.rend(); ++it)
		{
			Obj& o = **it;
			if (!o.selected) continue;
			PointF h[4]; RectF box;
			ObjHandleRects(o, h, box);
			if (PDist(p, DeletePos(box)) <= r) { obj = &o; return true; }
		}
		return false;
	}

	// ---------------- 绘制覆盖层 ----------------
	static void DrawSelectionLayer(Graphics& g)
	{
		Color box(220, 120, 190, 255);
		Color handleFill(245, 255, 255, 255);
		Color handleEdge(240, 60, 130, 235);
		Color vtxFill(240, 255, 255, 255);
		Color vtxEdge(250, 40, 170, 230);

		for (auto& up : doc.objs)
		{
			Obj& o = *up;
			if (!o.selected) continue;
			PointF h[4]; RectF boxRect;
			ObjHandleRects(o, h, boxRect);

			Pen bp(box, 1.5f);
			bp.SetDashStyle(DashStyleDash);
			g.DrawRectangle(&bp, boxRect);

			// 顶点
			std::vector<PointF> vs = ObjVertices(o);
			for (auto& v : vs)
			{
				SolidBrush fb(vtxFill);
				Pen ep(vtxEdge, 1.8f);
				g.FillEllipse(&fb, v.X - VertexR(), v.Y - VertexR(), VertexR() * 2, VertexR() * 2);
				g.DrawEllipse(&ep, v.X - VertexR(), v.Y - VertexR(), VertexR() * 2, VertexR() * 2);
			}
			// 四角圆形手柄
			for (int i = 0; i < 4; i++)
			{
				SolidBrush fb(handleFill);
				Pen ep(handleEdge, 2.0f);
				g.FillEllipse(&fb, h[i].X - HandleR(), h[i].Y - HandleR(), HandleR() * 2, HandleR() * 2);
				g.DrawEllipse(&ep, h[i].X - HandleR(), h[i].Y - HandleR(), HandleR() * 2, HandleR() * 2);
			}

			// 删除按钮（右上角外侧，红色圆 ×）
			{
				PointF dp = DeletePos(boxRect);
				float dr = DeleteR();
				SolidBrush db(Color(245, 232, 62, 62));
				Pen de(Color(255, 255, 255, 255), 2.0f);
				g.FillEllipse(&db, dp.X - dr, dp.Y - dr, dr * 2, dr * 2);
				g.DrawEllipse(&de, dp.X - dr, dp.Y - dr, dr * 2, dr * 2);
				float k = dr * 0.42f;
				Pen xx(Color(255, 255, 255, 255), 2.6f);
				xx.SetStartCap(LineCapRound);
				xx.SetEndCap(LineCapRound);
				g.DrawLine(&xx, dp.X - k, dp.Y - k, dp.X + k, dp.Y + k);
				g.DrawLine(&xx, dp.X - k, dp.Y + k, dp.X + k, dp.Y - k);
			}
		}
	}

	static void DrawEraserPreview(Graphics& g, const PointF& p, float radius, bool square)
	{
		Color fill(140, 255, 250, 240); // 乳白填充
		Color edge(235, 10, 10, 10);    // 黑描边
		SolidBrush fb(fill);
		Pen ep(edge, 2.5f);
		if (square)
		{
			float half = radius * 1.15f;
			RectF r(p.X - half, p.Y - half, half * 2, half * 2);
			g.FillRectangle(&fb, r);
			g.DrawRectangle(&ep, r);
		}
		else
		{
			g.FillEllipse(&fb, p.X - radius, p.Y - radius, radius * 2, radius * 2);
			g.DrawEllipse(&ep, p.X - radius, p.Y - radius, radius * 2, radius * 2);
		}
	}

	static void OverlayPaint()
	{
		if (!g_overlay) return;
		if (!g_cvOverlay.Ensure(g_vw, g_vh)) return;

		Graphics g(g_cvOverlay.hdc);
		g.SetSmoothingMode(SmoothingModeAntiAlias);
		g.SetTextRenderingHint(TextRenderingHintAntiAlias);
		// 全屏铺 1/255 的底色，保证分层窗口在透明区域仍能接收触控（alpha 为 0 会穿透）
		g.Clear(Color(1, 0, 0, 0));

		for (auto& o : doc.objs) DrawObject(g, *o);
		if (g_preview) DrawObject(g, *g_preview);

		if (g_tool == Tool::Lasso)
		{
			DrawSelectionLayer(g);
			if (g_drag == DragMode::Lasso && g_lasso.size() > 1)
			{
				Pen lp(Color(255, 235, 40, 40), 2.0f);
				lp.SetDashStyle(DashStyleDash);
				lp.SetDashCap(DashCapRound);
				std::vector<PointF> pts;
				for (auto& p : g_lasso) pts.push_back(PointF(p.X - g_vx, p.Y - g_vy));
				GraphicsPath path;
				for (size_t i = 1; i < pts.size(); i++)
				{
					path.AddLine(pts[i - 1], pts[i]);
				}
				g.DrawPath(&lp, &path);
			}
		}

		if (g_tool == Tool::Pen && g_drag == DragMode::Pen && g_penPts.size() > 1)
		{
			Color col = PenColorAt(cfg.penColor);
			col = Color((BYTE)kPenAlphas[cfg.penAlpha], col.GetR(), col.GetG(), col.GetB());
			Pen pen(col, kPenWidths[cfg.penWidth]);
			pen.SetStartCap(LineCapRound);
			pen.SetEndCap(LineCapRound);
			pen.SetLineJoin(LineJoinRound);
			std::vector<PointF> pts;
			for (auto& p : g_penPts) pts.push_back(PointF(p.X - g_vx, p.Y - g_vy));
			DrawStrokePoints(g, pen, pts);
		}

		if (g_eraseShow && (g_tool == Tool::Eraser || g_multiErase))
		{
			float radius = (g_multiErase ? (g_multiEraseIdx == 0 ? 15.0f : (g_multiEraseIdx == 1 ? 35.0f : 70.0f)) : UI_EraserRadius()) * UiScale();
			bool sq = g_multiErase ? (g_multiEraseIdx >= 3) : UI_EraserSquare();
			if (g_multiErase && g_multiErasePts.size() > 1)
			{
				for (auto& p : g_multiErasePts) DrawEraserPreview(g, PointF(p.X - g_vx, p.Y - g_vy), radius, sq);
			}
			else
			{
				DrawEraserPreview(g, PointF(g_erasePos.X - g_vx, g_erasePos.Y - g_vy), radius, sq);
			}
		}

		Present(g_overlay, g_cvOverlay, g_vx, g_vy);
		g_dirty = false;
	}

	// ---------------- 工具栏绘制 ----------------
	static void BarPaint(int side)
	{
		HWND hwnd = g_bar[side];
		if (!hwnd) return;
		int x, y, w, h;
		GetBarRect(side, x, y, w, h);
		if (!g_cvBar[side].Ensure(w, h)) return;

		Graphics g(g_cvBar[side].hdc);
		g.SetSmoothingMode(SmoothingModeAntiAlias);
		g.Clear(Color(0, 0, 0, 0));

		if (g_collapsed)
		{
			FillRound(g, RectF(0.5f, 0.5f, (float)w - 1.0f, (float)h - 1.0f), 12.0f * UiScale(),
				Color(238, 28, 30, 36), Color(120, 255, 255, 255));
			DrawToolIcon(g, 1, RectF(w * 0.22f, h * 0.22f, w * 0.56f, h * 0.56f), Color(255, 245, 245, 245));
		}
		else
		{
			FillRound(g, RectF(0.5f, 0.5f, (float)w - 1.0f, (float)h - 1.0f), 14.0f * UiScale(),
				Color(238, 26, 28, 34), Color(110, 255, 255, 255));

			int pad = BarPad(), gap = BarGap(), btn = BarBtn();
			Tool tools[7] = { Tool::Select, Tool::Pen, Tool::Eraser, Tool::Lasso, Tool::Shape, Tool::Select, Tool::Select };
			for (int i = 0; i < 7; i++)
			{
				RectF r((float)pad, (float)(pad + i * (btn + gap)), (float)(w - pad * 2), (float)btn);
				bool active = false;
				if (i < 5) active = (g_tool == tools[i]);

				if (active)
				{
					FillRound(g, r, 11.0f * UiScale(), Color(230, 58, 122, 235), Color(255, 120, 180, 255));
				}
				if (i == 5)
				{
					Pen sp(Color(60, 255, 255, 255), 1.0f);
					g.DrawLine(&sp, r.X + 4.0f, r.Y - gap * 0.5f, r.X + r.Width - 4.0f, r.Y - gap * 0.5f);
				}
				Color ic = active ? Color(255, 255, 255, 255) : Color(235, 222, 226, 234);
				DrawToolIcon(g, i, RectF(r.X + r.Width * 0.13f, r.Y + r.Height * 0.13f, r.Width * 0.74f, r.Height * 0.74f), ic);
			}
		}
		Present(hwnd, g_cvBar[side], x, y);
	}

	// ---------------- 弹出面板 ----------------
	static int g_popupMode = 0; // 1 图形 2 笔 3 橡皮
	static int g_popupSide = 0;

	struct PopupItem { RECT r; int value; };
	static std::vector<PopupItem> g_popupItems;

	static void PopupSize(int mode, int& W, int& H)
	{
		float s = UiScale();
		if (mode == 1)
		{
			int cell = (int)(64.0f * s), pad = (int)(10.0f * s), gap = (int)(6.0f * s);
			W = pad * 2 + cell * 3 + gap * 2;
			H = pad * 2 + cell * 4 + gap * 3;
		}
		else if (mode == 2)
		{
			W = (int)(218.0f * s);
			H = (int)(252.0f * s);
		}
		else
		{
			W = (int)(232.0f * s);
			H = (int)(84.0f * s);
		}
	}

	static void PopupLayout(int mode, int W, int H)
	{
		g_popupItems.clear();
		float s = UiScale();
		if (mode == 1)
		{
			int cell = (int)(64.0f * s), pad = (int)(10.0f * s), gap = (int)(6.0f * s);
			for (int i = 0; i < 11; i++)
			{
				int col = i % 3, row = i / 3;
				RECT r{ pad + col * (cell + gap), pad + row * (cell + gap), pad + col * (cell + gap) + cell, pad + row * (cell + gap) + cell };
				g_popupItems.push_back({ r, 100 + i });
			}
		}
		else if (mode == 2)
		{
			int pad = (int)(12.0f * s);
			int sw = (int)(40.0f * s), gap = (int)(8.0f * s);
			for (int i = 0; i < 9; i++)
			{
				int col = i % 3, row = i / 3;
				RECT r{ pad + col * (sw + gap), pad + row * (sw + gap), pad + col * (sw + gap) + sw, pad + row * (sw + gap) + sw };
				g_popupItems.push_back({ r, 1000 + i });
			}
			int y0 = pad + 3 * (sw + gap) + (int)(6.0f * s);
			int ww = (int)(26.0f * s), wgap = (int)(6.0f * s);
			for (int i = 0; i < 6; i++)
			{
				RECT r{ pad + i * (ww + wgap), y0, pad + i * (ww + wgap) + ww, y0 + (int)(30.0f * s) };
				g_popupItems.push_back({ r, 2000 + i });
			}
			int y1 = y0 + (int)(40.0f * s);
			int aw = (int)(42.0f * s), agap = (int)(6.0f * s);
			for (int i = 0; i < 4; i++)
			{
				RECT r{ pad + i * (aw + agap), y1, pad + i * (aw + agap) + aw, y1 + (int)(32.0f * s) };
				g_popupItems.push_back({ r, 3000 + i });
			}
		}
		else
		{
			int pad = (int)(12.0f * s);
			int bw = (int)(48.0f * s), gap = (int)(8.0f * s);
			for (int i = 0; i < 4; i++)
			{
				RECT r{ pad + i * (bw + gap), pad, pad + i * (bw + gap) + bw, pad + (int)(58.0f * s) };
				g_popupItems.push_back({ r, 4000 + i });
			}
		}
		(void)W; (void)H;
	}

	static Kind KindFromIndex(int i)
	{
		switch (i)
		{
		case 0: return Kind::Square;
		case 1: return Kind::Rect;
		case 2: return Kind::Triangle;
		case 3: return Kind::Parallelogram;
		case 4: return Kind::Trapezoid;
		case 5: return Kind::Circle;
		case 6: return Kind::Line;
		case 7: return Kind::Axes;
		case 8: return Kind::Ellipse;
		case 9: return Kind::Hyperbola;
		default: return Kind::Parabola;
		}
	}

	static void PopupPaint()
	{
		int W, H;
		PopupSize(g_popupMode, W, H);
		if (!g_cvPopup.Ensure(W, H)) return;

		Graphics g(g_cvPopup.hdc);
		g.SetSmoothingMode(SmoothingModeAntiAlias);
		g.Clear(Color(0, 0, 0, 0));
		FillRound(g, RectF(0.5f, 0.5f, (float)W - 1.0f, (float)H - 1.0f), 14.0f * UiScale(),
			Color(244, 26, 28, 34), Color(120, 255, 255, 255));

		PopupLayout(g_popupMode, W, H);
		for (auto& item : g_popupItems)
		{
			RectF r((float)item.r.left, (float)item.r.top, (float)(item.r.right - item.r.left), (float)(item.r.bottom - item.r.top));
			if (g_popupMode == 1)
			{
				int idx = item.value - 100;
				bool active = (g_shapeKind == KindFromIndex(idx));
				if (active) FillRound(g, r, 9.0f * UiScale(), Color(235, 58, 122, 235), Color(255, 120, 180, 255));
				RectF ib(r.X + r.Width * 0.18f, r.Y + r.Height * 0.18f, r.Width * 0.64f, r.Height * 0.64f);
				DrawShapePreview(g, KindFromIndex(idx), ib, Color(255, 238, 241, 248), 2.0f * UiScale());
			}
			else if (g_popupMode == 2)
			{
				int v = item.value;
				if (v >= 1000 && v < 2000)
				{
					int ci = v - 1000;
					bool active = (cfg.penColor == ci);
					Color c = PenColorAt(ci);
					SolidBrush b(c);
					g.FillEllipse(&b, r.X + 3, r.Y + 3, r.Width - 6, r.Height - 6);
					if (active)
					{
						Pen p(Color(255, 255, 255, 255), 2.5f);
						g.DrawEllipse(&p, r.X + 1, r.Y + 1, r.Width - 2, r.Height - 2);
					}
					else
					{
						Pen p(Color(90, 255, 255, 255), 1.0f);
						g.DrawEllipse(&p, r.X + 1, r.Y + 1, r.Width - 2, r.Height - 2);
					}
				}
				else if (v >= 2000 && v < 3000)
				{
					int wi = v - 2000;
					bool active = (cfg.penWidth == wi);
					if (active) FillRound(g, r, 7.0f * UiScale(), Color(200, 58, 122, 235), Color(255, 120, 180, 255));
					float wpx = kPenWidths[wi];
					if (wpx > r.Height - 8.0f) wpx = r.Height - 8.0f;
					SolidBrush b(Color(255, 240, 242, 248));
					g.FillEllipse(&b, r.X + r.Width * 0.5f - wpx * 0.5f, r.Y + r.Height * 0.5f - wpx * 0.5f, wpx, wpx);
				}
				else if (v >= 3000 && v < 4000)
				{
					int ai = v - 3000;
					bool active = (cfg.penAlpha == ai);
					if (active) FillRound(g, r, 7.0f * UiScale(), Color(200, 58, 122, 235), Color(255, 120, 180, 255));
					SolidBrush b(Color((BYTE)kPenAlphas[ai], 240, 242, 248));
					g.FillEllipse(&b, r.X + r.Width * 0.5f - 9.0f, r.Y + r.Height * 0.5f - 9.0f, 18.0f, 18.0f);
				}
			}
			else
			{
				int ei = item.value - 4000;
				bool active = (cfg.eraserSize == ei);
				if (active) FillRound(g, r, 8.0f * UiScale(), Color(220, 58, 122, 235), Color(255, 120, 180, 255));
				Color fill(90, 255, 252, 240);
				Color edge(230, 20, 20, 20);
				SolidBrush fb(fill);
				Pen ep(edge, 2.0f);
				if (ei == 3)
				{
					RectF rr(r.X + 8, r.Y + 8, r.Width - 16, r.Height - 16);
					g.FillRectangle(&fb, rr);
					g.DrawRectangle(&ep, rr);
				}
				else
				{
					float rr = (float)(item.r.right - item.r.left) * 0.32f + ei * 3.0f;
					g.FillEllipse(&fb, r.X + r.Width * 0.5f - rr, r.Y + r.Height * 0.5f - rr, rr * 2, rr * 2);
					g.DrawEllipse(&ep, r.X + r.Width * 0.5f - rr, r.Y + r.Height * 0.5f - rr, rr * 2, rr * 2);
				}
			}
		}

		int x, y, w, h;
		GetBarRect(g_popupSide, x, y, w, h);
		int px = (g_popupSide == 0) ? (x + w + 6) : (x - W - 6);
		int py = y;
		RECT mon = MonitorRect();
		if (px < mon.left) px = mon.left;
		if (px + W > mon.right) px = mon.right - W;
		if (py + H > mon.bottom) py = mon.bottom - H;
		if (py < mon.top) py = mon.top;
		Present(g_popup, g_cvPopup, px, py);
	}

	static void ClosePopup()
	{
		if (g_popup) ShowWindow(g_popup, SW_HIDE);
		g_popupMode = 0;
		g_popupItems.clear();
	}

	static void OpenPopup(int mode)
	{
		g_popupMode = mode;
		g_popupSide = 0;
		POINT pt;
		GetCursorPos(&pt);
		RECT r0, r1;
		GetWindowRect(g_bar[0], &r0);
		GetWindowRect(g_bar[1], &r1);
		int d0 = (pt.x - r0.left) * (pt.x - r0.left) + (pt.y - r0.top) * (pt.y - r0.top);
		int d1 = (pt.x - r1.left) * (pt.x - r1.left) + (pt.y - r1.top) * (pt.y - r1.top);
		if (d1 < d0) g_popupSide = 1;

		int W, H;
		PopupSize(mode, W, H);
		if (!g_popup)
		{
			g_popup = CreateWindowExW(
				WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE,
				L"AnnoPopupWnd", L"AnnoPopup",
				WS_POPUP, 0, 0, W, H, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
		}
		else
		{
			SetWindowPos(g_popup, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		}
		ShowWindow(g_popup, SW_SHOWNOACTIVATE);
		PopupPaint();
	}

	// ---------------- 工具切换 ----------------
	static void UpdateClickThrough()
	{
		if (!g_overlay) return;
		LONG_PTR ex = GetWindowLongPtrW(g_overlay, GWL_EXSTYLE);
		LONG_PTR want = ex;
		if (g_tool == Tool::Select) want |= WS_EX_TRANSPARENT;
		else want &= ~WS_EX_TRANSPARENT;
		if (want != ex) SetWindowLongPtrW(g_overlay, GWL_EXSTYLE, want);
		SetWindowPos(g_overlay, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		for (int i = 0; i < 2; i++)
			if (g_bar[i]) SetWindowPos(g_bar[i], HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		if (g_popup && IsWindowVisible(g_popup))
			SetWindowPos(g_popup, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	}

	Tool UI_CurrentTool() { return g_tool; }
	Kind UI_CurrentShape() { return g_shapeKind; }
	int UI_EraserIndex() { return cfg.eraserSize; }
	bool UI_EraserSquare() { return cfg.eraserSize == 3; }

	float UI_EraserRadius()
	{
		switch (cfg.eraserSize)
		{
		case 0: return 15.0f;
		case 1: return 35.0f;
		case 2: return 70.0f;
		default: return 80.0f;
		}
	}

	void UI_ApplyTool(Tool t)
	{
		g_tool = t;
		if (t != Tool::Lasso) doc.ClearSelection();
		if (t != Tool::Eraser) { g_eraseShow = false; }
		ClosePopup();
		UpdateClickThrough();
		UI_RefreshToolbars();
		g_dirty = true;
	}

	void UI_SetShapeKind(Kind k)
	{
		g_shapeKind = k;
		g_tool = Tool::Shape;
		doc.ClearSelection();
		ClosePopup();
		UpdateClickThrough();
		UI_RefreshToolbars();
		g_dirty = true;
	}

	void UI_SetEraserIndex(int idx)
	{
		cfg.eraserSize = idx < 0 ? 0 : (idx > 3 ? 3 : idx);
		SaveConfig();
		if (g_popupMode == 3) PopupPaint();
		UI_RefreshToolbars();
		g_dirty = true;
	}

	void UI_CycleEraser()
	{
		cfg.eraserSize = (cfg.eraserSize + 1) % 4;
		SaveConfig();
		g_dirty = true;
	}

	void UI_SetToolbarCollapsed(bool collapsed)
	{
		if (g_collapsed == collapsed) return;
		g_collapsed = collapsed;
		cfg.toolbarCollapsed = collapsed;
		SaveConfig();
		ClosePopup();
		UI_RepositionToolbars();
		UpdateClickThrough();
	}

	void UI_RefreshToolbars()
	{
		for (int i = 0; i < 2; i++)
		{
			if (!g_bar[i]) continue;
			int x, y, w, h;
			GetBarRect(i, x, y, w, h);
			SetWindowPos(g_bar[i], HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
			BarPaint(i);
		}
	}

	void UI_RepositionToolbars()
	{
		UI_RefreshToolbars();
	}

	void UI_InvalidateToolbars() { UI_RefreshToolbars(); }
	void UI_Invalidate() { g_dirty = true; }

	bool UI_OverlayWindowPointInToolbar(int x, int y)
	{
		for (int i = 0; i < 2; i++)
		{
			if (!g_bar[i]) continue;
			RECT r;
			GetWindowRect(g_bar[i], &r);
			if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) return true;
		}
		return false;
	}

	// ---------------- 输入处理 ----------------
	static void PenBegin(const PointF& p)
	{
		g_drag = DragMode::Pen;
		g_penPts.clear();
		g_penPts.push_back(p);
		g_last = p;
		g_dirty = true;
	}

	static void PenMove(const PointF& p)
	{
		if (PDist(p, g_last) < 1.5f) return;
		g_penPts.push_back(p);
		g_last = p;
		g_dirty = true;
	}

	static void PenEnd()
	{
		if (g_penPts.size() == 1) g_penPts.push_back(PointF(g_penPts[0].X + 0.5f, g_penPts[0].Y + 0.5f));
		if (g_penPts.size() >= 2)
		{
			Color c = PenColorAt(cfg.penColor);
			auto o = MakeStroke(g_penPts, c, kPenWidths[cfg.penWidth], kPenAlphas[cfg.penAlpha]);
			doc.Add(std::move(o));
		}
		g_penPts.clear();
		g_drag = DragMode::None;
		g_dirty = true;
	}

	static void EraseAtPoint(const PointF& p, float radius, bool square)
	{
		doc.EraseAt(p, radius, square);
		g_dirty = true;
	}

	static void InputDown(const PointF& p)
	{
		ClosePopup();
		g_eraseShow = false;

		switch (g_tool)
		{
		case Tool::Pen:
			PenBegin(p);
			break;
		case Tool::Eraser:
			g_drag = DragMode::Erase;
			EraseAtPoint(p, UI_EraserRadius() * UiScale(), UI_EraserSquare());
			g_erasePos = p;
			g_eraseShow = true;
			break;
		case Tool::Lasso:
		{
			Obj* obj = nullptr;
			int idx = 0;
			Obj* delObj = nullptr;
			if (HitSelectedDelete(p, delObj))
			{
				doc.objs.erase(std::remove_if(doc.objs.begin(), doc.objs.end(),
					[delObj](const std::unique_ptr<Obj>& q) { return q.get() == delObj; }), doc.objs.end());
				g_drag = DragMode::None;
				g_dirty = true;
				break;
			}
			if (HitSelectedHandle(p, obj, idx))
			{
				g_drag = DragMode::Handle;
				g_xformObj = obj;
				g_xformHandle = idx;
				g_xStartRot = obj->rot;
				PointF d = PSub(p, obj->c);
				g_xStartAngle = AngleOf(d);
				g_xStartDist = PLen(d);
				if (g_xStartDist < 1.0f) g_xStartDist = 1.0f;
				g_xStartLocal = obj->lp;
			}
			else if (HitSelectedVertex(p, obj, idx))
			{
				g_drag = DragMode::Vertex;
				g_vertexObj = obj;
				g_vertexIdx = idx;
			}
			else if (doc.AnySelected() && HitSelectedArea(p))
			{
				g_drag = DragMode::MoveSel;
				g_last = p;
			}
			else
			{
				doc.ClearSelection();
				g_drag = DragMode::Lasso;
				g_lasso.clear();
				g_lasso.push_back(p);
				g_last = p;
			}
			g_dirty = true;
			break;
		}
		case Tool::Shape:
			g_drag = DragMode::ShapeCreate;
			g_p0 = p;
			g_preview = MakeShape(g_shapeKind, p, PointF(p.X + 1, p.Y + 1), PenColorAt(cfg.penColor), kPenWidths[cfg.penWidth], kPenAlphas[cfg.penAlpha]);
			g_dirty = true;
			break;
		default:
			break;
		}
	}

	static void InputMove(const PointF& p)
	{
		g_erasePos = p;
		if (g_tool == Tool::Eraser) { g_eraseShow = true; g_dirty = true; }

		switch (g_drag)
		{
		case DragMode::Pen: PenMove(p); break;
		case DragMode::Erase: EraseAtPoint(p, UI_EraserRadius() * UiScale(), UI_EraserSquare()); break;
		case DragMode::Lasso:
			if (PDist(p, g_last) > 2.0f) { g_lasso.push_back(p); g_last = p; g_dirty = true; }
			break;
		case DragMode::ShapeCreate:
			g_preview = MakeShape(g_shapeKind, g_p0, p, PenColorAt(cfg.penColor), kPenWidths[cfg.penWidth], kPenAlphas[cfg.penAlpha]);
			g_dirty = true;
			break;
		case DragMode::Handle:
			if (g_xformObj)
			{
				PointF d = PSub(p, g_xformObj->c);
				float dist = PLen(d);
				float ang = AngleOf(d);
				float s = dist / g_xStartDist;
				if (s < 0.05f) s = 0.05f;
				if (s > 30.0f) s = 30.0f;
				g_xformObj->lp = g_xStartLocal;
				for (auto& lp : g_xformObj->lp) lp = PMul(lp, s);
				// 手写笔迹只缩放，不旋转
				if (g_xformObj->kind != Kind::Stroke)
					g_xformObj->rot = g_xStartRot + (ang - g_xStartAngle);
				g_dirty = true;
			}
			break;
		case DragMode::MoveSel:
		{
			float dx = p.X - g_last.X, dy = p.Y - g_last.Y;
			if (dx != 0.0f || dy != 0.0f)
			{
				doc.MoveSelected(dx, dy);
				g_last = p;
				g_dirty = true;
			}
			break;
		}
		case DragMode::Vertex:
			if (g_vertexObj)
			{
				ObjDragVertex(*g_vertexObj, g_vertexIdx, p);
				g_dirty = true;
			}
			break;
		default:
			break;
		}
	}

	static void InputUp(const PointF& p)
	{
		switch (g_drag)
		{
		case DragMode::Pen:
			PenEnd();
			break;
		case DragMode::Erase:
			g_eraseShow = true;
			break;
		case DragMode::Lasso:
			if (g_lasso.size() >= 3)
			{
				doc.SelectLasso(g_lasso);
				g_dirty = true;
			}
			g_lasso.clear();
			break;
		case DragMode::ShapeCreate:
			if (g_preview)
			{
				RectF b = ObjWorldBounds(*g_preview);
				if (b.Width > 6.0f || b.Height > 6.0f)
				{
					doc.Add(std::move(g_preview));
					// 画完图形后默认切到「选中」工具
					g_tool = Tool::Lasso;
					UpdateClickThrough();
					UI_RefreshToolbars();
				}
				g_preview.reset();
				g_dirty = true;
			}
			break;
		case DragMode::Handle:
			g_xformObj = nullptr;
			break;
		case DragMode::MoveSel:
			break;
		case DragMode::Vertex:
			g_vertexObj = nullptr;
			g_vertexIdx = -1;
			break;
		default:
			break;
		}
		g_drag = DragMode::None;
		(void)p;
		g_dirty = true;
	}

	// ---------------- 覆盖层窗口 ----------------
	static HWND g_floating = nullptr;
	static BOOL CALLBACK FindFloatingProc(HWND h, LPARAM)
	{
		wchar_t title[128] = {};
		GetWindowTextW(h, title, 128);
		if (wcsstr(title, L"FloatingWindow"))
		{
			DWORD pid = 0;
			GetWindowThreadProcessId(h, &pid);
			if (pid == GetCurrentProcessId()) { g_floating = h; return FALSE; }
		}
		return TRUE;
	}

	// 若智绘教自带的悬浮窗与本工具右侧栏重叠，则把悬浮窗向左让位
	static void AvoidFloatingOverlap()
	{
		if (!g_bar[1]) return;
		g_floating = nullptr;
		EnumWindows(FindFloatingProc, 0);
		if (!g_floating) return;
		RECT fr{}, br{};
		if (!GetWindowRect(g_floating, &fr)) return;
		GetWindowRect(g_bar[1], &br);
		if (fr.right <= br.left - 8 || fr.bottom <= br.top || fr.top >= br.bottom) return;
		int shift = fr.right - (br.left - 8);
		int nx = fr.left - shift;
		if (nx < 0) return;
		SetWindowPos(g_floating, nullptr, nx, fr.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	}

	static void RaiseToolbars()
	{
		if (!g_visible) return;
		for (int i = 0; i < 2; i++)
			if (g_bar[i]) SetWindowPos(g_bar[i], HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		if (g_popup && IsWindowVisible(g_popup))
			SetWindowPos(g_popup, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	}

	static LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		switch (msg)
		{
		case WM_MOUSEACTIVATE:
			return MA_NOACTIVATE;
		case WM_SETCURSOR:
			if (LOWORD(lParam) == HTCLIENT)
			{
				if (g_tool == Tool::Pen || g_tool == Tool::Eraser) { SetCursor(nullptr); return TRUE; }
			}
			break;
		case WM_LBUTTONDOWN:
			SetCapture(hwnd);
			InputDown(W(PointF((float)GET_X_LPARAM(lParam), (float)GET_Y_LPARAM(lParam))));
			return 0;
		case WM_MOUSEMOVE:
			InputMove(W(PointF((float)GET_X_LPARAM(lParam), (float)GET_Y_LPARAM(lParam))));
			return 0;
		case WM_LBUTTONUP:
			ReleaseCapture();
			InputUp(W(PointF((float)GET_X_LPARAM(lParam), (float)GET_Y_LPARAM(lParam))));
			return 0;
		case WM_RBUTTONUP:
			if (cfg.rightClickSelect)
			{
				UI_ApplyTool(g_tool == Tool::Select ? Tool::Pen : Tool::Select);
			}
			return 0;
		case WM_POINTERDOWN:
		case WM_POINTERUPDATE:
		case WM_POINTERUP:
		{
			UINT32 pid = GET_POINTERID_WPARAM(wParam);
			POINTER_INPUT_TYPE pt = PT_POINTER;
			if (!GetPointerType(pid, &pt)) return 0;
			if (pt == PT_MOUSE) return 0;

			POINTER_INFO pi{};
			if (!GetPointerInfo(pid, &pi)) return 0;
			PointF p((float)pi.ptPixelLocation.x, (float)pi.ptPixelLocation.y);

			if (pt == PT_TOUCH)
			{
				if (msg == WM_POINTERDOWN)
				{
					g_touchIds.insert(pid);
					if (cfg.multiFinger && g_touchIds.size() >= 2 && g_tool == Tool::Pen)
					{
						// 取消当前笔迹，进入多指橡皮
						g_penPts.clear();
						if (g_drag == DragMode::Pen) g_drag = DragMode::None;
						g_multiErase = true;
						g_multiEraseIdx = (g_touchIds.size() >= 4) ? 3 : (g_touchIds.size() == 3 ? 1 : 0);
						g_multiErasePts.clear();
						g_dirty = true;
					}
					if (g_multiErase)
					{
						g_multiErasePts.push_back(p);
						float r = (g_multiEraseIdx == 0 ? 15.0f : (g_multiEraseIdx == 1 ? 35.0f : 70.0f)) * UiScale();
						EraseAtPoint(p, r, g_multiEraseIdx >= 3);
						g_dirty = true;
						return 0;
					}
				}
				else if (msg == WM_POINTERUP)
				{
					g_touchIds.erase(pid);
					if (g_touchIds.size() < 2) g_multiErase = false;
					if (g_touchIds.empty()) { g_multiErasePts.clear(); g_eraseShow = false; }
					g_dirty = true;
					if (g_multiErase || g_drag != DragMode::None) return 0;
				}
				else
				{
					if (g_multiErase)
					{
						float r = (g_multiEraseIdx == 0 ? 15.0f : (g_multiEraseIdx == 1 ? 35.0f : 70.0f)) * UiScale();
						EraseAtPoint(p, r, g_multiEraseIdx >= 3);
						return 0;
					}
				}
			}

			if (msg == WM_POINTERDOWN) { SetCapture(hwnd); InputDown(p); }
			else if (msg == WM_POINTERUPDATE) InputMove(p);
			else if (msg == WM_POINTERUP) { ReleaseCapture(); InputUp(p); }
			return 0;
		}
		case WM_POINTERCAPTURECHANGED:
			if (g_drag != DragMode::None) InputUp(g_last);
			return 0;
		case WM_TIMER:
			if (wParam == 1)
			{
				if (g_dirty) OverlayPaint();
				static int raiseTick = 0;
				if (++raiseTick >= 30)
				{
					raiseTick = 0;
					RaiseToolbars();
				}
				static bool overlapFixed = false;
				if (!overlapFixed && raiseTick == 15)
				{
					overlapFixed = true;
					AvoidFloatingOverlap();
				}
			}
			return 0;
		default:
			break;
		}
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}

	// ---------------- 工具栏窗口 ----------------
	struct BarState { bool drag = false; bool moved = false; POINT startCursor{ 0,0 }; RECT startRect{ 0,0,0,0 }; };
	static BarState g_barState[2];

	static int BarButtonAt(int side, int mx, int my)
	{
		if (g_collapsed) return 0;
		int pad = BarPad(), gap = BarGap(), btn = BarBtn();
		for (int i = 0; i < 7; i++)
		{
			int y0 = pad + i * (btn + gap);
			if (mx >= pad && mx <= BarW() - pad && my >= y0 && my <= y0 + btn) return i;
		}
		return -1;
	}

	static void BarAction(int side, int btn)
	{
		switch (btn)
		{
		case 0: UI_ApplyTool(Tool::Select); break;
		case 1:
			if (g_tool == Tool::Pen) OpenPopup(2);
			else UI_ApplyTool(Tool::Pen);
			break;
		case 2:
			if (g_tool == Tool::Eraser) OpenPopup(3);
			else { UI_ApplyTool(Tool::Eraser); g_eraseShow = true; }
			break;
		case 3: UI_ApplyTool(Tool::Lasso); break;
		case 4: OpenPopup(1); break;
		case 5:
			doc.Clear();
			doc.ClearSelection();
			g_dirty = true;
			UI_RefreshToolbars();
			break;
		case 6: UI_SetToolbarCollapsed(true); break;
		default: break;
		}
	}

	static LRESULT CALLBACK BarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		int side = (int)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
		if (side != 0 && side != 1) side = 0;

		switch (msg)
		{
		case WM_MOUSEACTIVATE:
			return MA_NOACTIVATE;
		case WM_LBUTTONDOWN:
		{
			int mx = GET_X_LPARAM(lParam), my = GET_Y_LPARAM(lParam);
			int btn = BarButtonAt(side, mx, my);
			if (g_collapsed)
			{
				POINT pt;
				GetCursorPos(&pt);
				g_barState[side].drag = true;
				g_barState[side].moved = false;
				g_barState[side].startCursor = pt;
				SetCapture(hwnd);
			}
			else if (btn >= 0)
			{
				BarAction(side, btn);
			}
			else
			{
				POINT pt;
				GetCursorPos(&pt);
				GetWindowRect(hwnd, &g_barState[side].startRect);
				g_barState[side].startCursor = pt;
				g_barState[side].drag = true;
				g_barState[side].moved = false;
				SetCapture(hwnd);
			}
			return 0;
		}
		case WM_MOUSEMOVE:
		{
			if (!g_barState[side].drag) return 0;
			POINT pt;
			GetCursorPos(&pt);
			int dx = pt.x - g_barState[side].startCursor.x;
			int dy = pt.y - g_barState[side].startCursor.y;
			if (abs(dx) + abs(dy) > 5) g_barState[side].moved = true;
			if (g_barState[side].moved)
			{
				if (!g_collapsed)
				{
					int nx = g_barState[side].startRect.left + dx;
					int ny = g_barState[side].startRect.top + dy;
					if (side == 0) { cfg.leftX = nx; cfg.leftY = ny; }
					else { cfg.rightX = nx + (g_barState[side].startRect.right - g_barState[side].startRect.left); cfg.rightY = ny; }
					SetWindowPos(hwnd, HWND_TOPMOST, nx, ny, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
				}
			}
			return 0;
		}
		case WM_LBUTTONUP:
		{
			if (g_barState[side].drag)
			{
				ReleaseCapture();
				bool moved = g_barState[side].moved;
				g_barState[side].drag = false;
				if (g_collapsed && !moved)
				{
					UI_SetToolbarCollapsed(false);
				}
				else if (moved)
				{
					SaveConfig();
					BarPaint(0);
					BarPaint(1);
				}
			}
			return 0;
		}
		default:
			break;
		}
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}

	// ---------------- 弹出面板窗口 ----------------
	static LRESULT CALLBACK PopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		switch (msg)
		{
		case WM_MOUSEACTIVATE:
			return MA_NOACTIVATE;
		case WM_LBUTTONUP:
		{
			int mx = GET_X_LPARAM(lParam), my = GET_Y_LPARAM(lParam);
			for (auto& item : g_popupItems)
			{
				if (mx >= item.r.left && mx <= item.r.right && my >= item.r.top && my <= item.r.bottom)
				{
					int v = item.value;
					if (v >= 100 && v < 200) UI_SetShapeKind(KindFromIndex(v - 100));
					else if (v >= 1000 && v < 2000) { cfg.penColor = v - 1000; SaveConfig(); PopupPaint(); UI_RefreshToolbars(); }
					else if (v >= 2000 && v < 3000) { cfg.penWidth = v - 2000; SaveConfig(); PopupPaint(); }
					else if (v >= 3000 && v < 4000) { cfg.penAlpha = v - 3000; SaveConfig(); PopupPaint(); }
					else if (v >= 4000) { UI_SetEraserIndex(v - 4000); PopupPaint(); }
					return 0;
				}
			}
			ClosePopup();
			return 0;
		}
		default:
			break;
		}
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}

	// ---------------- 创建 / 销毁 ----------------
	static void RegisterClasses()
	{
		static bool done = false;
		if (done) return;
		done = true;
		WNDCLASSEXW wc{};
		wc.cbSize = sizeof(wc);
		wc.style = CS_DBLCLKS;
		wc.lpfnWndProc = OverlayProc;
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.hCursor = nullptr;
		wc.lpszClassName = L"AnnoOverlayWnd";
		RegisterClassExW(&wc);

		wc.lpfnWndProc = BarProc;
		wc.lpszClassName = L"AnnoBarWnd";
		RegisterClassExW(&wc);

		wc.lpfnWndProc = PopupProc;
		wc.lpszClassName = L"AnnoPopupWnd";
		RegisterClassExW(&wc);
	}

	void UI_Create()
	{
		RegisterClasses();

		g_vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
		g_vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
		g_vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
		g_vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
		if (g_vw <= 0) g_vw = GetSystemMetrics(SM_CXSCREEN);
		if (g_vh <= 0) g_vh = GetSystemMetrics(SM_CYSCREEN);

		g_overlay = CreateWindowExW(
			WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE,
			L"AnnoOverlayWnd", L"AnnoOverlay",
			WS_POPUP, g_vx, g_vy, g_vw, g_vh,
			nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
		if (g_overlay)
		{
			ShowWindow(g_overlay, SW_SHOWNOACTIVATE);
			SetTimer(g_overlay, 1, 30, nullptr);
		}

		g_collapsed = cfg.toolbarCollapsed;
		for (int i = 0; i < 2; i++)
		{
			int x, y, w, h;
			GetBarRect(i, x, y, w, h);
			g_bar[i] = CreateWindowExW(
				WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE,
				L"AnnoBarWnd", L"AnnoBar",
				WS_POPUP, x, y, w, h,
				nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
			SetWindowLongPtrW(g_bar[i], GWLP_USERDATA, (LONG_PTR)i);
			ShowWindow(g_bar[i], SW_SHOWNOACTIVATE);
		}

		UI_ApplyTool(Tool::Select);
		UI_RefreshToolbars();
		RaiseToolbars();
	}

	void UI_Destroy()
	{
		if (g_overlay) { KillTimer(g_overlay, 1); DestroyWindow(g_overlay); g_overlay = nullptr; }
		for (int i = 0; i < 2; i++)
		{
			if (g_bar[i]) { DestroyWindow(g_bar[i]); g_bar[i] = nullptr; }
			g_cvBar[i].Destroy();
		}
		if (g_popup) { DestroyWindow(g_popup); g_popup = nullptr; }
		g_cvOverlay.Destroy();
		g_cvPopup.Destroy();
	}

	void AnnoSetVisible(bool vis)
	{
		g_visible = vis;
		if (g_overlay) ShowWindow(g_overlay, vis ? SW_SHOWNOACTIVATE : SW_HIDE);
		for (int i = 0; i < 2; i++)
			if (g_bar[i]) ShowWindow(g_bar[i], vis ? SW_SHOWNOACTIVATE : SW_HIDE);
		if (!vis) ClosePopup();
		if (vis) { UI_RefreshToolbars(); g_dirty = true; }
	}

	bool AnnoVisible() { return g_visible; }

	void AnnoToggleVisible() { AnnoSetVisible(!g_visible); }
}
