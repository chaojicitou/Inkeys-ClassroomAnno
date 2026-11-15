#pragma once

// 课堂批注模块 —— 独立于上游智绘教画板的矢量批注层
// 说明：本模块刻意不包含 IdtMain.h，避免上游宏定义（NOMINMAX 失效等）干扰；
//       所有几何坐标均为屏幕物理像素。

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <memory>
#include <vector>
#include <string>
#include <cmath>

namespace anno
{
	using namespace Gdiplus;

	constexpr float kPi = 3.14159265358979323846f;

	// ---------- 数学工具 ----------
	inline float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
	inline float Absf(float v) { return v < 0.0f ? -v : v; }
	inline PointF PAdd(const PointF& a, const PointF& b) { return PointF(a.X + b.X, a.Y + b.Y); }
	inline PointF PSub(const PointF& a, const PointF& b) { return PointF(a.X - b.X, a.Y - b.Y); }
	inline PointF PMul(const PointF& a, float s) { return PointF(a.X * s, a.Y * s); }
	inline PointF PMid(const PointF& a, const PointF& b) { return PointF((a.X + b.X) * 0.5f, (a.Y + b.Y) * 0.5f); }
	inline float PLen(const PointF& a) { return sqrtf(a.X * a.X + a.Y * a.Y); }
	inline float PDist(const PointF& a, const PointF& b) { return PLen(PSub(a, b)); }
	inline float Dot(const PointF& a, const PointF& b) { return a.X * b.X + a.Y * b.Y; }
	inline float Cross(const PointF& a, const PointF& b) { return a.X * b.Y - a.Y * b.X; }
	inline PointF PRot(const PointF& a, float rad)
	{
		float c = cosf(rad), s = sinf(rad);
		return PointF(a.X * c - a.Y * s, a.X * s + a.Y * c);
	}
	inline PointF PNorm(const PointF& a)
	{
		float l = PLen(a);
		return l > 0.00001f ? PointF(a.X / l, a.Y / l) : PointF(1.0f, 0.0f);
	}
	inline PointF Perp(const PointF& a) { return PointF(-a.Y, a.X); }
	inline float AngleOf(const PointF& v) { return atan2f(v.Y, v.X); }

	inline float DistToSegment(const PointF& p, const PointF& a, const PointF& b)
	{
		PointF ab = PSub(b, a);
		float l2 = Dot(ab, ab);
		if (l2 < 0.00001f) return PDist(p, a);
		float t = Clampf(Dot(PSub(p, a), ab) / l2, 0.0f, 1.0f);
		return PDist(p, PAdd(a, PMul(ab, t)));
	}

	inline bool PointInPolygon(const std::vector<PointF>& poly, const PointF& p)
	{
		bool in = false;
		size_t n = poly.size();
		for (size_t i = 0, j = n ? n - 1 : 0; i < n; j = i++)
		{
			const PointF& a = poly[i];
			const PointF& b = poly[j];
			if (((a.Y > p.Y) != (b.Y > p.Y)) &&
				(p.X < (b.X - a.X) * (p.Y - a.Y) / (b.Y - a.Y + 0.000001f) + a.X))
				in = !in;
		}
		return in;
	}

	inline bool SegmentIntersect(const PointF& a, const PointF& b, const PointF& c, const PointF& d)
	{
		float d1 = Cross(PSub(b, a), PSub(c, a));
		float d2 = Cross(PSub(b, a), PSub(d, a));
		float d3 = Cross(PSub(d, c), PSub(a, c));
		float d4 = Cross(PSub(d, c), PSub(b, c));
		return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
	}

	// ---------- 图形种类 ----------
	enum class Kind
	{
		Stroke = 0,
		Square,          // 正方形（四个顶点自由拖动）
		Rect,            // 长方形
		Triangle,        // 三角形
		Parallelogram,   // 平行四边形
		Trapezoid,       // 梯形
		Circle,          // 圆
		Line,            // 直线
		Axes,            // 坐标轴
		Ellipse,         // 椭圆（标焦点）
		Hyperbola,       // 双曲线（标焦点）
		Parabola         // 抛物线（标焦点）
	};

	enum class Tool
	{
		Select = 0,  // 选择：整窗鼠标穿透
		Pen,         // 画笔
		Eraser,      // 橡皮
		Lasso,       // 选中（套索）
		Shape        // 图形
	};

	// ---------- 矢量对象 ----------
	// 几何统一存放在「未旋转的局部坐标系」中，世界坐标 = c + R(rot) * local
	struct Obj
	{
		int id = 0;
		Kind kind = Kind::Stroke;
		PointF c{ 0.0f, 0.0f };
		float rot = 0.0f;
		std::vector<PointF> lp;
		Color color = Color(255, 235, 45, 45);
		float width = 5.0f;
		int alpha = 255;
		bool selected = false;

		Color StrokeColor() const { return Color((BYTE)Clampf((float)alpha, 0.0f, 255.0f), color.GetR(), color.GetG(), color.GetB()); }
	};

	// ---------- 配置 ----------
	struct Config
	{
		int penColor = 0;
		int penWidth = 2;
		int penAlpha = 0;
		int eraserSize = 1;      // 0:30 1:70 2:140 3:方形超大
		bool multiFinger = true;
		bool rightClickSelect = true;
		bool toolbarCollapsed = false;
		bool autoStart = false;
		int leftX = -1, leftY = -1;
		int rightX = -1, rightY = -1;
	};
	extern Config cfg;
	void LoadConfig();
	void SaveConfig();

	extern const int kPenColorCount;
	extern const BYTE kPenColors[9][3];
	extern const int kPenWidthCount;
	extern const float kPenWidths[6];
	extern const int kPenAlphaCount;
	extern const int kPenAlphas[4];

	inline Color PenColorAt(int idx)
	{
		int i = idx < 0 ? 0 : (idx >= 9 ? 8 : idx);
		return Color(255, kPenColors[i][0], kPenColors[i][1], kPenColors[i][2]);
	}

	// ---------- 几何 / 渲染 ----------
	PointF LocalToWorld(const Obj& o, const PointF& lp);
	PointF WorldToLocal(const Obj& o, const PointF& wp);
	void RecenterObj(Obj& o);
	void ObjScale(Obj& o, float s);
	void ObjRotateTo(Obj& o, float rad);

	// 轮廓采样（世界坐标）
	void ObjOutline(const Obj& o, std::vector<PointF>& out, int samples = 64);
	// 水平包围盒（世界坐标）
	RectF ObjWorldBounds(const Obj& o);
	bool ObjHit(const Obj& o, const PointF& p, float tol);

	// 可拖拽顶点（世界坐标）；Stroke 返回空
	int ObjVertexCount(const Obj& o);
	std::vector<PointF> ObjVertices(const Obj& o);
	// 拖拽第 idx 个顶点到世界坐标 wp，内部保持图形本质属性
	void ObjDragVertex(Obj& o, int idx, const PointF& wp);

	void DrawObject(Graphics& g, const Obj& o);
	void DrawShapePreview(Graphics& g, Kind k, const RectF& box, const Color& col, float stroke = 2.0f);
	void BuildSmoothPath(GraphicsPath& path, const std::vector<PointF>& pts);
	void DrawStrokePoints(Graphics& g, Pen& pen, const std::vector<PointF>& pts);

	// ---------- 文档 ----------
	struct Doc
	{
		std::vector<std::unique_ptr<Obj>> objs;
		int nextId = 1;

		void Clear();
		Obj* Add(std::unique_ptr<Obj> o);
		void RemoveSelected();
		Obj* TopHit(const PointF& p, float tol);
		void SelectLasso(const std::vector<PointF>& poly);
		void ClearSelection();
		bool AnySelected() const;
		RectF SelectionBounds() const;
		void MoveSelected(float dx, float dy);
		void EraseStrokesAt(const PointF& p, float radius, bool square);
		void EraseShapesAt(const PointF& p, float radius, bool square);
		void EraseAt(const PointF& p, float radius, bool square);
	};
	extern Doc doc;

	// 由点集生成笔迹对象
	std::unique_ptr<Obj> MakeStroke(const std::vector<PointF>& pts, const Color& col, float width, int alpha);
	// 由拖拽框生成图形
	std::unique_ptr<Obj> MakeShape(Kind k, const PointF& a, const PointF& b, const Color& col, float width, int alpha);

	// ---------- UI / 应用 ----------
	void AnnoMain();          // 线程入口
	void AnnoBootLog(const char* tag, long long v);
	void AnnoToggleVisible();
	void AnnoSetVisible(bool vis);
	bool AnnoVisible();
	void AnnoRegisterHotkeys(HWND hwnd);

	void UI_Create();
	void UI_Destroy();
	void UI_Invalidate();
	void UI_InvalidateToolbars();
	void UI_ApplyTool(Tool t);
	Tool UI_CurrentTool();
	void UI_SetShapeKind(Kind k);
	Kind UI_CurrentShape();
	void UI_SetToolbarCollapsed(bool collapsed);
	void UI_RefreshToolbars();
	void UI_RepositionToolbars();
	void UI_CycleEraser();
	void UI_SetEraserIndex(int idx);
	int UI_EraserIndex();
	float UI_EraserRadius();
	bool UI_EraserSquare();
	bool UI_OverlayWindowPointInToolbar(int x, int y);

	extern HWND g_overlay;
}
