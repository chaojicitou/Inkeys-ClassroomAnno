﻿#include "AnnoCore.h"

namespace anno
{
	const int kPenColorCount = 9;
	const BYTE kPenColors[9][3] =
	{
		{ 235,  45,  45 }, // 红
		{ 245, 135,  35 }, // 橙
		{ 245, 205,  45 }, // 黄
		{  45, 190,  95 }, // 绿
		{  35, 185, 195 }, // 青
		{  45, 110, 235 }, // 蓝
		{ 155,  85, 220 }, // 紫
		{  35,  35,  35 }, // 黑
		{ 255, 255, 255 }  // 白
	};
	const int kPenWidthCount = 6;
	const float kPenWidths[6] = { 2.0f, 3.5f, 5.0f, 8.0f, 12.0f, 18.0f };
	const int kPenAlphaCount = 4;
	const int kPenAlphas[4] = { 255, 200, 150, 100 };

	PointF LocalToWorld(const Obj& o, const PointF& lp) { return PAdd(o.c, PRot(lp, o.rot)); }
	PointF WorldToLocal(const Obj& o, const PointF& wp) { return PRot(PSub(wp, o.c), -o.rot); }

	static bool IsFreeForm(Kind k)
	{
		return k == Kind::Stroke || k == Kind::Square || k == Kind::Rect || k == Kind::Triangle ||
			k == Kind::Parallelogram || k == Kind::Trapezoid || k == Kind::Line;
	}

	void RecenterObj(Obj& o)
	{
		if (o.lp.empty() || !IsFreeForm(o.kind)) return;
		PointF sum(0.0f, 0.0f);
		for (const auto& p : o.lp) sum = PAdd(sum, p);
		PointF m = PMul(sum, 1.0f / (float)o.lp.size());
		for (auto& p : o.lp) p = PSub(p, m);
		o.c = PAdd(o.c, PRot(m, o.rot));
	}

	void ObjScale(Obj& o, float s)
	{
		for (auto& p : o.lp) p = PMul(p, s);
	}

	void ObjRotateTo(Obj& o, float rad) { o.rot = rad; }

	// ---------------- 轮廓 ----------------
	static void DownloadHyperbola(const Obj& o, std::vector<PointF>& out, int samples)
	{
		float a = Absf(o.lp[0].X), b = Absf(o.lp[1].Y);
		if (a < 1.0f) a = 1.0f;
		if (b < 1.0f) b = 1.0f;
		const float tmax = 1.1f;
		for (int i = 0; i <= samples; i++)
		{
			float t = -tmax + 2.0f * tmax * (float)i / (float)samples;
			out.push_back(LocalToWorld(o, PointF(a * coshf(t), b * sinhf(t))));
		}
		for (int i = 0; i <= samples; i++)
		{
			float t = -tmax + 2.0f * tmax * (float)i / (float)samples;
			out.push_back(LocalToWorld(o, PointF(-a * coshf(t), b * sinhf(t))));
		}
	}

	void ObjOutline(const Obj& o, std::vector<PointF>& out, int samples)
	{
		out.clear();
		if (o.lp.empty()) { out.push_back(o.c); return; }
		if (samples < 8) samples = 8;

		switch (o.kind)
		{
		case Kind::Stroke:
		case Kind::Square:
		case Kind::Rect:
		case Kind::Triangle:
		case Kind::Parallelogram:
		case Kind::Trapezoid:
		case Kind::Line:
			for (const auto& p : o.lp) out.push_back(LocalToWorld(o, p));
			break;
		case Kind::Circle:
		{
			float r = PLen(o.lp[0]);
			for (int i = 0; i <= samples; i++)
			{
				float a = 2.0f * kPi * (float)i / (float)samples;
				out.push_back(LocalToWorld(o, PointF(r * cosf(a), r * sinf(a))));
			}
			break;
		}
		case Kind::Ellipse:
		{
			float a = Absf(o.lp[0].X), b = Absf(o.lp.size() > 1 ? o.lp[1].Y : 0.0f);
			for (int i = 0; i <= samples; i++)
			{
				float t = 2.0f * kPi * (float)i / (float)samples;
				out.push_back(LocalToWorld(o, PointF(a * cosf(t), b * sinf(t))));
			}
			break;
		}
		case Kind::Axes:
		{
			float xl = Absf(o.lp[0].X);
			float yl = Absf(o.lp.size() > 1 ? o.lp[1].Y : 0.0f);
			out.push_back(LocalToWorld(o, PointF(-xl, 0.0f)));
			out.push_back(LocalToWorld(o, PointF(xl, 0.0f)));
			out.push_back(LocalToWorld(o, PointF(0.0f, -yl)));
			out.push_back(LocalToWorld(o, PointF(0.0f, yl)));
			break;
		}
		case Kind::Hyperbola:
			if (o.lp.size() > 1) DownloadHyperbola(o, out, samples / 2);
			break;
		case Kind::Parabola:
		{
			float p = Absf(o.lp[0].Y);
			float hw = Absf(o.lp.size() > 1 ? o.lp[1].X : 60.0f);
			if (p < 1.0f) p = 1.0f;
			for (int i = 0; i <= samples; i++)
			{
				float x = -hw + 2.0f * hw * (float)i / (float)samples;
				out.push_back(LocalToWorld(o, PointF(x, -(x * x) / (4.0f * p))));
			}
			break;
		}
		default:
			break;
		}
	}

	RectF ObjWorldBounds(const Obj& o)
	{
		std::vector<PointF> pts;
		ObjOutline(o, pts, 64);
		if (pts.empty()) return RectF(o.c.X, o.c.Y, 0.0f, 0.0f);
		float minx = pts[0].X, maxx = pts[0].X, miny = pts[0].Y, maxy = pts[0].Y;
		for (const auto& p : pts)
		{
			if (p.X < minx) minx = p.X;
			if (p.X > maxx) maxx = p.X;
			if (p.Y < miny) miny = p.Y;
			if (p.Y > maxy) maxy = p.Y;
		}
		return RectF(minx, miny, maxx - minx, maxy - miny);
	}

	bool ObjHit(const Obj& o, const PointF& p, float tol)
	{
		if (o.kind == Kind::Axes)
		{
			// 坐标轴：命中轴线或原点附近
			PointF l = WorldToLocal(o, p);
			float xl = Absf(o.lp[0].X);
			float yl = Absf(o.lp.size() > 1 ? o.lp[1].Y : 0.0f);
			if (Absf(l.Y) <= tol + 4.0f && Absf(l.X) <= xl + tol) return true;
			if (Absf(l.X) <= tol + 4.0f && Absf(l.Y) <= yl + tol) return true;
			return false;
		}
		std::vector<PointF> pts;
		ObjOutline(o, pts, 72);
		if (pts.empty()) return false;
		float t = tol + o.width * 0.5f;
		if (pts.size() == 1) return PDist(pts[0], p) <= t;
		for (size_t i = 0; i + 1 < pts.size(); i++)
		{
			if (DistToSegment(p, pts[i], pts[i + 1]) <= t) return true;
		}
		// 闭合图形补上首尾
		if (o.kind == Kind::Square || o.kind == Kind::Rect || o.kind == Kind::Triangle ||
			o.kind == Kind::Parallelogram || o.kind == Kind::Trapezoid || o.kind == Kind::Circle ||
			o.kind == Kind::Ellipse)
			if (DistToSegment(p, pts.back(), pts.front()) <= t) return true;
		return false;
	}

	int ObjVertexCount(const Obj& o)
	{
		switch (o.kind)
		{
		case Kind::Stroke: return 0;
		case Kind::Square: case Kind::Rect: case Kind::Parallelogram: case Kind::Trapezoid: return 4;
		case Kind::Triangle: return 3;
		case Kind::Circle: case Kind::Axes: case Kind::Ellipse: case Kind::Hyperbola: case Kind::Parabola: return 2;
		case Kind::Line: return 2;
		default: return 0;
		}
	}

	std::vector<PointF> ObjVertices(const Obj& o)
	{
		std::vector<PointF> v;
		// 手写笔迹不提供顶点编辑，只允许整体缩放
		if (o.kind == Kind::Stroke) return v;
		if (o.kind == Kind::Line)
		{
			for (auto& p : o.lp) v.push_back(LocalToWorld(o, p));
			return v;
		}
		if (o.kind == Kind::Axes)
		{
			float xl = Absf(o.lp[0].X);
			float yl = Absf(o.lp.size() > 1 ? o.lp[1].Y : 0.0f);
			v.push_back(LocalToWorld(o, PointF(xl, 0.0f)));
			v.push_back(LocalToWorld(o, PointF(0.0f, -yl)));
			return v;
		}
		if (o.kind == Kind::Circle || o.kind == Kind::Ellipse || o.kind == Kind::Hyperbola || o.kind == Kind::Parabola)
		{
			for (auto& p : o.lp) v.push_back(LocalToWorld(o, p));
			return v;
		}
		for (auto& p : o.lp) v.push_back(LocalToWorld(o, p));
		return v;
	}

	// ---------------- 顶点拖拽 ----------------
	static void DragRectVertex(Obj& o, int idx, const PointF& q)
	{
		if (o.lp.size() < 4) return;
		int opp = (idx + 2) % 4;
		PointF a = o.lp[opp];
		float minx = q.X < a.X ? q.X : a.X;
		float maxx = q.X > a.X ? q.X : a.X;
		float miny = q.Y < a.Y ? q.Y : a.Y;
		float maxy = q.Y > a.Y ? q.Y : a.Y;
		if (maxx - minx < 4.0f) maxx = minx + 4.0f;
		if (maxy - miny < 4.0f) maxy = miny + 4.0f;
		o.lp[0] = PointF(minx, miny);
		o.lp[1] = PointF(maxx, miny);
		o.lp[2] = PointF(maxx, maxy);
		o.lp[3] = PointF(minx, maxy);
	}

	static void DragTrapezoidVertex(Obj& o, int idx, const PointF& q)
	{
		if (o.lp.size() < 4) return;
		PointF A = o.lp[0];
		PointF B = o.lp[1];
		PointF TR = o.lp[2];
		PointF TL = o.lp[3];
		PointF dir = PNorm(PSub(B, A));
		PointF n(dir.Y, -dir.X); // 向上
		float o0 = Dot(PSub(TL, A), dir);
		float h = Dot(PSub(TL, A), n);
		float topLen = Dot(PSub(TR, TL), dir);
		if (topLen < 6.0f) topLen = 6.0f;
		if (h > -6.0f && h < 6.0f) h = (h < 0.0f ? -6.0f : 6.0f);

		if (idx == 0) A = q;
		else if (idx == 1) B = q;
		else if (idx == 3)
		{
			o0 = Dot(PSub(q, A), dir);
			h = Dot(PSub(q, A), n);
			if (h > -6.0f && h < 6.0f) h = (h < 0.0f ? -6.0f : 6.0f);
		}
		else if (idx == 2)
		{
			h = Dot(PSub(q, A), n);
			if (h > -6.0f && h < 6.0f) h = (h < 0.0f ? -6.0f : 6.0f);
			PointF tlNow = PAdd(A, PAdd(PMul(dir, o0), PMul(n, h)));
			topLen = Dot(PSub(q, tlNow), dir);
			if (topLen < 6.0f) topLen = 6.0f;
		}

		dir = PNorm(PSub(B, A));
		n = PointF(dir.Y, -dir.X);
		o.lp[0] = A;
		o.lp[1] = B;
		TL = PAdd(A, PAdd(PMul(dir, o0), PMul(n, h)));
		TR = PAdd(TL, PMul(dir, topLen));
		o.lp[3] = TL;
		o.lp[2] = TR;
	}

	void ObjDragVertex(Obj& o, int idx, const PointF& wp)
	{
		if (idx < 0) return;
		PointF q = WorldToLocal(o, wp);

		switch (o.kind)
		{
		case Kind::Square:
		case Kind::Triangle:
			if (idx < (int)o.lp.size()) o.lp[idx] = q;
			RecenterObj(o);
			return;
		case Kind::Rect:
			DragRectVertex(o, idx, q);
			RecenterObj(o);
			return;
		case Kind::Parallelogram:
			if (o.lp.size() >= 4 && idx < 4)
			{
				PointF d = PSub(q, o.lp[idx]);
				o.lp[idx] = q;
				o.lp[(idx + 1) % 4] = PAdd(o.lp[(idx + 1) % 4], d);
				o.lp[(idx + 3) % 4] = PAdd(o.lp[(idx + 3) % 4], d);
				RecenterObj(o);
			}
			return;
		case Kind::Trapezoid:
			DragTrapezoidVertex(o, idx, q);
			RecenterObj(o);
			return;
		case Kind::Line:
			if (idx < (int)o.lp.size()) o.lp[idx] = q;
			RecenterObj(o);
			return;
		case Kind::Circle:
		{
			float r = PLen(q);
			if (r < 3.0f) r = 3.0f;
			PointF d = PNorm(q);
			o.lp[0] = PMul(d, r);
			return;
		}
		case Kind::Ellipse:
		case Kind::Hyperbola:
			if (o.lp.size() < 2) return;
			if (idx == 0)
			{
				float a = Absf(q.X);
				if (a < 3.0f) a = 3.0f;
				o.lp[0] = PointF(a, 0.0f);
			}
			else
			{
				float b = Absf(q.Y);
				if (b < 3.0f) b = 3.0f;
				o.lp[1] = PointF(0.0f, b);
			}
			return;
		case Kind::Parabola:
			if (o.lp.size() < 2) return;
			if (idx == 0)
			{
				float p = Absf(q.Y);
				if (p < 3.0f) p = 3.0f;
				o.lp[0] = PointF(0.0f, -p);
			}
			else
			{
				float hw = Absf(q.X);
				if (hw < 8.0f) hw = 8.0f;
				o.lp[1] = PointF(hw, 0.0f);
			}
			return;
		case Kind::Axes:
			if (o.lp.size() < 2) return;
			if (idx == 0)
			{
				float xl = Absf(q.X);
				if (xl < 20.0f) xl = 20.0f;
				o.lp[0] = PointF(xl, 0.0f);
			}
			else
			{
				float yl = Absf(q.Y);
				if (yl < 20.0f) yl = 20.0f;
				o.lp[1] = PointF(0.0f, yl);
			}
			return;
		default:
			return;
		}
	}

	// ---------------- 平滑笔迹 ----------------
	void BuildSmoothPath(GraphicsPath& path, const std::vector<PointF>& pts)
	{
		if (pts.empty()) return;
		if (pts.size() == 1)
		{
			path.AddLine(pts[0], PointF(pts[0].X + 0.1f, pts[0].Y));
			return;
		}
		if (pts.size() == 2)
		{
			path.AddLine(pts[0], pts[1]);
			return;
		}
		PointF cur = pts[0];
		path.StartFigure();
		for (size_t i = 1; i + 1 < pts.size(); i++)
		{
			PointF mid = PMid(pts[i], pts[i + 1]);
			PointF c1 = PAdd(cur, PMul(PSub(pts[i], cur), 2.0f / 3.0f));
			PointF c2 = PAdd(mid, PMul(PSub(pts[i], mid), 2.0f / 3.0f));
			path.AddBezier(cur, c1, c2, mid);
			cur = mid;
		}
		path.AddLine(cur, pts.back());
	}

	void DrawStrokePoints(Graphics& g, Pen& pen, const std::vector<PointF>& pts)
	{
		GraphicsPath path;
		BuildSmoothPath(path, pts);
		g.DrawPath(&pen, &path);
	}

	// ---------------- 焦点 ----------------
	struct FontHolder
	{
		Font* font = nullptr;
		~FontHolder() { delete font; }
	};
	static Font* LabelFont()
	{
		static FontHolder h;
		if (!h.font) h.font = new Font(L"Microsoft YaHei", 13.0f, FontStyleRegular, UnitPixel);
		return h.font;
	}

	static void DrawDot(Graphics& g, const PointF& p, float r, const Color& c)
	{
		SolidBrush b(c);
		g.FillEllipse(&b, p.X - r, p.Y - r, r * 2.0f, r * 2.0f);
	}

	static void DrawLabel(Graphics& g, const wchar_t* text, const PointF& p, const Color& c)
	{
		SolidBrush b(c);
		Font* f = LabelFont();
		g.DrawString(text, -1, f, p, &b);
	}

	// ---------------- 绘制 ----------------
	static void DrawAxes(Graphics& g, const Obj& o)
	{
		if (o.lp.size() < 2) return;
		float xl = Absf(o.lp[0].X);
		float yl = Absf(o.lp[1].Y);
		if (xl < 20.0f) xl = 20.0f;
		if (yl < 20.0f) yl = 20.0f;
		Color rc(o.alpha, 220, 45, 45);
		Pen pen(rc, 1.4f);
		pen.SetLineJoin(LineJoinRound);

		g.DrawLine(&pen, -xl, 0.0f, xl, 0.0f);
		g.DrawLine(&pen, 0.0f, -yl, 0.0f, yl);

		// 箭头
		{
			float al = 10.0f;
			g.DrawLine(&pen, xl, 0.0f, xl - al, -al * 0.5f);
			g.DrawLine(&pen, xl, 0.0f, xl - al, al * 0.5f);
			g.DrawLine(&pen, 0.0f, -yl, -al * 0.5f, -yl + al);
			g.DrawLine(&pen, 0.0f, -yl, al * 0.5f, -yl + al);
		}
		// 密集实心坐标点
		{
			const float step = 16.0f;
			float r = 1.7f;
			SolidBrush b(rc);
			for (float x = step; x <= xl; x += step)
			{
				g.FillEllipse(&b, x - r, -r, r * 2, r * 2);
				g.FillEllipse(&b, -x - r, -r, r * 2, r * 2);
			}
			for (float y = step; y <= yl; y += step)
			{
				g.FillEllipse(&b, -r, -y - r, r * 2, r * 2);
				g.FillEllipse(&b, -r, y - r, r * 2, r * 2);
			}
			g.FillEllipse(&b, -3.0f, -3.0f, 6.0f, 6.0f);
		}
		// 轴标签
		DrawLabel(g, L"x", PointF(xl + 7.0f, -20.0f), rc);
		DrawLabel(g, L"y", PointF(6.0f, -yl - 22.0f), rc);
	}

	static void DrawFoci(Graphics& g, const Obj& o)
	{
		Color fc(o.alpha, 235, 60, 60);
		if (o.kind == Kind::Ellipse && o.lp.size() > 1)
		{
			float a = Absf(o.lp[0].X), b = Absf(o.lp[1].Y);
			if (a >= b) { float c = sqrtf(a * a - b * b); DrawDot(g, PointF(-c, 0), 3.2f, fc); DrawDot(g, PointF(c, 0), 3.2f, fc); DrawLabel(g, L"F1", PointF(-c + 5, 4), fc); DrawLabel(g, L"F2", PointF(c + 5, 4), fc); }
			else { float c = sqrtf(b * b - a * a); DrawDot(g, PointF(0, -c), 3.2f, fc); DrawDot(g, PointF(0, c), 3.2f, fc); DrawLabel(g, L"F1", PointF(6, -c - 6), fc); DrawLabel(g, L"F2", PointF(6, c - 6), fc); }
		}
		else if (o.kind == Kind::Hyperbola && o.lp.size() > 1)
		{
			float a = Absf(o.lp[0].X), b = Absf(o.lp[1].Y);
			float c = sqrtf(a * a + b * b);
			DrawDot(g, PointF(-c, 0), 3.2f, fc); DrawDot(g, PointF(c, 0), 3.2f, fc);
			DrawLabel(g, L"F1", PointF(-c - 24, 4), fc); DrawLabel(g, L"F2", PointF(c + 6, 4), fc);
		}
		else if (o.kind == Kind::Parabola && !o.lp.empty())
		{
			DrawDot(g, o.lp[0], 3.4f, fc);
			DrawLabel(g, L"F", PointF(o.lp[0].X + 6, o.lp[0].Y - 6), fc);
		}
	}

	void DrawObject(Graphics& g, const Obj& o)
	{
		if (o.lp.empty()) return;
		Color col = o.StrokeColor();
		Pen pen(col, o.width);
		pen.SetLineJoin(LineJoinRound);
		pen.SetStartCap(LineCapRound);
		pen.SetEndCap(LineCapRound);

		GraphicsState st = g.Save();
		g.TranslateTransform(o.c.X, o.c.Y);
		g.RotateTransform(o.rot * 180.0f / kPi);
		g.SetSmoothingMode(SmoothingModeAntiAlias);

		switch (o.kind)
		{
		case Kind::Stroke:
			DrawStrokePoints(g, pen, o.lp);
			break;
		case Kind::Square:
		case Kind::Rect:
		case Kind::Triangle:
		case Kind::Parallelogram:
		case Kind::Trapezoid:
		{
			GraphicsPath path;
			path.AddPolygon(o.lp.data(), (INT)o.lp.size());
			g.DrawPath(&pen, &path);
			break;
		}
		case Kind::Line:
			if (o.lp.size() >= 2) g.DrawLine(&pen, o.lp[0], o.lp[1]);
			break;
		case Kind::Circle:
		{
			float r = PLen(o.lp[0]);
			g.DrawEllipse(&pen, -r, -r, r * 2.0f, r * 2.0f);
			break;
		}
		case Kind::Ellipse:
		{
			float a = Absf(o.lp[0].X);
			float b = Absf(o.lp.size() > 1 ? o.lp[1].Y : 0.0f);
			g.DrawEllipse(&pen, -a, -b, a * 2.0f, b * 2.0f);
			DrawFoci(g, o);
			break;
		}
		case Kind::Axes:
			DrawAxes(g, o);
			break;
		case Kind::Hyperbola:
		{
			if (o.lp.size() < 2) break;
			float a = Absf(o.lp[0].X), b = Absf(o.lp[1].Y);
			if (a < 1.0f) a = 1.0f;
			if (b < 1.0f) b = 1.0f;
			const int N = 48;
			const float tmax = 1.1f;
			GraphicsPath path;
			for (int s = 0; s < 2; s++)
			{
				float sign = s == 0 ? 1.0f : -1.0f;
				path.StartFigure();
				PointF prev(0.0f, 0.0f);
				for (int i = 0; i <= N; i++)
				{
					float t = -tmax + 2.0f * tmax * (float)i / (float)N;
					PointF p(sign * a * coshf(t), b * sinhf(t));
					if (i == 0) path.AddLine(p, p);
					else path.AddLine(prev, p);
					prev = p;
				}
			}
			g.DrawPath(&pen, &path);
			DrawFoci(g, o);
			break;
		}
		case Kind::Parabola:
		{
			if (o.lp.size() < 2) break;
			float p = Absf(o.lp[0].Y);
			float hw = Absf(o.lp[1].X);
			if (p < 1.0f) p = 1.0f;
			GraphicsPath path;
			const int N = 64;
			PointF prev(0.0f, 0.0f);
			for (int i = 0; i <= N; i++)
			{
				float x = -hw + 2.0f * hw * (float)i / (float)N;
				PointF q(x, -(x * x) / (4.0f * p));
				if (i == 0) path.StartFigure(), path.AddLine(q, q);
				else path.AddLine(prev, q);
				prev = q;
			}
			g.DrawPath(&pen, &path);
			DrawFoci(g, o);
			break;
		}
		default:
			break;
		}
		g.Restore(st);
	}

	// ---------------- 创建 ----------------
	std::unique_ptr<Obj> MakeStroke(const std::vector<PointF>& pts, const Color& col, float width, int alpha)
	{
		auto o = std::make_unique<Obj>();
		o->kind = Kind::Stroke;
		o->color = Color(255, col.GetR(), col.GetG(), col.GetB());
		o->width = width;
		o->alpha = alpha;
		if (pts.empty()) return o;
		PointF sum(0.0f, 0.0f);
		for (const auto& p : pts) sum = PAdd(sum, p);
		o->c = PMul(sum, 1.0f / (float)pts.size());
		for (const auto& p : pts) o->lp.push_back(PSub(p, o->c));
		return o;
	}

	std::unique_ptr<Obj> MakeShape(Kind k, const PointF& a, const PointF& b, const Color& col, float width, int alpha)
	{
		auto o = std::make_unique<Obj>();
		o->kind = k;
		o->color = Color(255, col.GetR(), col.GetG(), col.GetB());
		o->width = width;
		o->alpha = alpha;

		float x0 = a.X < b.X ? a.X : b.X;
		float x1 = a.X > b.X ? a.X : b.X;
		float y0 = a.Y < b.Y ? a.Y : b.Y;
		float y1 = a.Y > b.Y ? a.Y : b.Y;
		float w = x1 - x0, h = y1 - y0;
		if (w < 4.0f) w = 4.0f;
		if (h < 4.0f) h = 4.0f;
		PointF center(x0 + w * 0.5f, y0 + h * 0.5f);

		switch (k)
		{
		case Kind::Square:
		{
			float s = w < h ? w : h;
			PointF cc(x0 + s * 0.5f, y0 + s * 0.5f);
			o->c = cc;
			o->lp = { PointF(-s / 2, -s / 2), PointF(s / 2, -s / 2), PointF(s / 2, s / 2), PointF(-s / 2, s / 2) };
			break;
		}
		case Kind::Rect:
			o->c = center;
			o->lp = { PointF(-w / 2, -h / 2), PointF(w / 2, -h / 2), PointF(w / 2, h / 2), PointF(-w / 2, h / 2) };
			break;
		case Kind::Triangle:
			o->c = center;
			o->lp = { PointF(-w / 2, h / 2), PointF(w / 2, h / 2), PointF(0, -h / 2) };
			break;
		case Kind::Parallelogram:
		{
			float off = w * 0.25f;
			o->c = center;
			o->lp = { PointF(-w / 2 + off, h / 2), PointF(w / 2, h / 2), PointF(w / 2 - off, -h / 2), PointF(-w / 2, -h / 2) };
			break;
		}
		case Kind::Trapezoid:
		{
			float in = w * 0.22f;
			o->c = center;
			o->lp = { PointF(-w / 2, h / 2), PointF(w / 2, h / 2), PointF(w / 2 - in, -h / 2), PointF(-w / 2 + in, -h / 2) };
			break;
		}
		case Kind::Circle:
		{
			float r = (w < h ? w : h) * 0.5f;
			o->c = center;
			o->lp = { PointF(r, 0) };
			break;
		}
		case Kind::Line:
			o->c = PMid(a, b);
			o->lp = { PSub(a, o->c), PSub(b, o->c) };
			break;
		case Kind::Axes:
			o->c = center;
			o->lp = { PointF(w / 2.0f, 0.0f), PointF(0.0f, h / 2.0f) };
			break;
		case Kind::Ellipse:
			o->c = center;
			o->lp = { PointF(w / 2, 0), PointF(0, h / 2) };
			break;
		case Kind::Hyperbola:
			o->c = center;
			o->lp = { PointF(w * 0.36f, 0), PointF(0, h / 2) };
			break;
		case Kind::Parabola:
		{
			float hw = w / 2.0f;
			float p = (hw * hw) / (4.0f * h);
			o->c = PointF(center.X, y1);
			o->lp = { PointF(0, -p), PointF(hw, 0) };
			break;
		}
		default:
			break;
		}
		return o;
	}

	// ---------------- 预览图标 ----------------
	void DrawShapePreview(Graphics& g, Kind k, const RectF& box, const Color& col, float stroke)
	{
		g.SetSmoothingMode(SmoothingModeAntiAlias);
		Pen pen(col, stroke);
		pen.SetLineJoin(LineJoinRound);
		pen.SetStartCap(LineCapRound);
		pen.SetEndCap(LineCapRound);
		SolidBrush brush(col);

		float x = box.X, y = box.Y, w = box.Width, h = box.Height;
		PointF lt(x, y), rt(x + w, y), rb(x + w, y + h), lb(x, y + h);
		PointF ct(x + w * 0.5f, y), cb(x + w * 0.5f, y + h);

		switch (k)
		{
		case Kind::Square:
		{
			float s = w < h ? w : h;
			RectF r(x + (w - s) * 0.5f, y + (h - s) * 0.5f, s, s);
			g.DrawRectangle(&pen, r);
			break;
		}
		case Kind::Rect:
			g.DrawRectangle(&pen, box);
			break;
		case Kind::Triangle:
		{
			PointF p[3] = { lb, rb, ct };
			g.DrawPolygon(&pen, p, 3);
			break;
		}
		case Kind::Parallelogram:
		{
			PointF p[4] = { PointF(x + w * 0.22f, y + h), rb, PointF(x + w * 0.78f, y), PointF(x, y) };
			g.DrawPolygon(&pen, p, 4);
			break;
		}
		case Kind::Trapezoid:
		{
			PointF p[4] = { lb, rb, PointF(x + w * 0.78f, y), PointF(x + w * 0.22f, y) };
			g.DrawPolygon(&pen, p, 4);
			break;
		}
		case Kind::Circle:
			g.DrawEllipse(&pen, x, y + (h - w) * 0.5f, w, w);
			break;
		case Kind::Line:
			g.DrawLine(&pen, PointF(x + 2, y + h - 2), PointF(x + w - 2, y + 2));
			break;
		case Kind::Axes:
		{
			float ox = x + w * 0.42f, oy = y + h * 0.62f;
			g.DrawLine(&pen, ox, oy, x + w - 2, oy);
			g.DrawLine(&pen, ox, oy, ox, y + 2);
			for (float i = 8; i < w * 0.55f; i += 8)
			{
				g.FillEllipse(&brush, ox + i - 1.0f, oy - 1.0f, 2.0f, 2.0f);
				g.FillEllipse(&brush, ox - i - 1.0f, oy - 1.0f, 2.0f, 2.0f);
			}
			for (float i = 8; i < h * 0.55f; i += 8)
				g.FillEllipse(&brush, ox - 1.0f, oy - i - 1.0f, 2.0f, 2.0f);
			g.FillEllipse(&brush, ox - 1.8f, oy - 1.8f, 3.6f, 3.6f);
			break;
		}
		case Kind::Ellipse:
			g.DrawEllipse(&pen, box);
			g.FillEllipse(&brush, x + w * 0.18f - 1.8f, y + h * 0.5f - 1.8f, 3.6f, 3.6f);
			g.FillEllipse(&brush, x + w * 0.82f - 1.8f, y + h * 0.5f - 1.8f, 3.6f, 3.6f);
			break;
		case Kind::Hyperbola:
		{
			float a = w * 0.30f, b = h * 0.42f;
			float cx = x + w * 0.5f, cy = y + h * 0.5f;
			for (int s = 0; s < 2; s++)
			{
				float sign = s == 0 ? 1.0f : -1.0f;
				GraphicsPath path;
				PointF prev(0.0f, 0.0f);
				for (int i = 0; i <= 24; i++)
				{
					float t = -1.9f + 3.8f * (float)i / 24.0f;
					PointF p(cx + sign * a * coshf(t), cy + b * sinhf(t));
					if (i == 0) path.StartFigure(), path.AddLine(p, p);
					else path.AddLine(prev, p);
					prev = p;
				}
				g.DrawPath(&pen, &path);
			}
			g.FillEllipse(&brush, cx - a * 1.6f - 1.6f, cy - 1.6f, 3.2f, 3.2f);
			g.FillEllipse(&brush, cx + a * 1.6f - 1.6f, cy - 1.6f, 3.2f, 3.2f);
			break;
		}
		case Kind::Parabola:
		{
			float hw = w * 0.45f;
			float vx = x + w * 0.5f, vy = y + h - 4.0f;
			float p = (hw * hw) / (4.0f * (h - 8.0f));
			GraphicsPath path;
			PointF prev(0.0f, 0.0f);
			for (int i = 0; i <= 32; i++)
			{
				float xx = -hw + 2.0f * hw * (float)i / 32.0f;
				PointF q(vx + xx, vy - (xx * xx) / (4.0f * p));
				if (i == 0) path.StartFigure(), path.AddLine(q, q);
				else path.AddLine(prev, q);
				prev = q;
			}
			g.DrawPath(&pen, &path);
			g.FillEllipse(&brush, vx - 1.8f, vy - p - 1.8f, 3.6f, 3.6f);
			break;
		}
		default:
			break;
		}
	}
}
