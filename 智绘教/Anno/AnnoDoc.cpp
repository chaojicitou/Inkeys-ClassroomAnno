#include "AnnoCore.h"
#include <algorithm>

namespace anno
{
	Doc doc;

	void Doc::Clear()
	{
		objs.clear();
		nextId = 1;
	}

	Obj* Doc::Add(std::unique_ptr<Obj> o)
	{
		if (!o) return nullptr;
		o->id = nextId++;
		Obj* raw = o.get();
		objs.push_back(std::move(o));
		return raw;
	}

	void Doc::RemoveSelected()
	{
		objs.erase(
			std::remove_if(objs.begin(), objs.end(),
				[](const std::unique_ptr<Obj>& p) { return p->selected; }),
			objs.end());
	}

	Obj* Doc::TopHit(const PointF& p, float tol)
	{
		for (auto it = objs.rbegin(); it != objs.rend(); ++it)
		{
			if (ObjHit(**it, p, tol)) return it->get();
		}
		return nullptr;
	}

	static bool ObjInLasso(const Obj& o, const std::vector<PointF>& poly)
	{
		if (poly.size() < 3) return false;
		std::vector<PointF> out;
		ObjOutline(o, out, 48);
		if (out.empty()) return false;

		for (const auto& p : out)
			if (PointInPolygon(poly, p)) return true;

		if (out.size() == 1)
			return PointInPolygon(poly, out[0]);

		for (size_t i = 0; i + 1 < poly.size(); i++)
		{
			for (size_t j = 0; j + 1 < out.size(); j++)
			{
				if (SegmentIntersect(poly[i], poly[i + 1], out[j], out[j + 1])) return true;
			}
			if (SegmentIntersect(poly[i], poly[i + 1], out.back(), out.front())) return true;
		}
		return false;
	}

	void Doc::SelectLasso(const std::vector<PointF>& poly)
	{
		for (auto& o : objs)
			o->selected = ObjInLasso(*o, poly);
	}

	void Doc::ClearSelection()
	{
		for (auto& o : objs) o->selected = false;
	}

	bool Doc::AnySelected() const
	{
		for (auto& o : objs) if (o->selected) return true;
		return false;
	}

	RectF Doc::SelectionBounds() const
	{
		bool first = true;
		float minx = 0, miny = 0, maxx = 0, maxy = 0;
		for (auto& o : objs)
		{
			if (!o->selected) continue;
			RectF r = ObjWorldBounds(*o);
			if (first) { minx = r.X; miny = r.Y; maxx = r.X + r.Width; maxy = r.Y + r.Height; first = false; }
			else
			{
				if (r.X < minx) minx = r.X;
				if (r.Y < miny) miny = r.Y;
				if (r.X + r.Width > maxx) maxx = r.X + r.Width;
				if (r.Y + r.Height > maxy) maxy = r.Y + r.Height;
			}
		}
		return RectF(minx, miny, maxx - minx, maxy - miny);
	}

	void Doc::MoveSelected(float dx, float dy)
	{
		for (auto& o : objs)
			if (o->selected) o->c = PAdd(o->c, PointF(dx, dy));
	}

	static bool InEraser(const PointF& e, float radius, bool square, const PointF& p, float grow)
	{
		float r = radius + grow;
		if (square)
		{
			float half = r * 1.15f;
			return Absf(p.X - e.X) <= half && Absf(p.Y - e.Y) <= half;
		}
		return PDist(p, e) <= r;
	}

	void Doc::EraseStrokesAt(const PointF& p, float radius, bool square)
	{
		std::vector<std::unique_ptr<Obj>> added;
		for (auto it = objs.begin(); it != objs.end();)
		{
			Obj& o = **it;
			if (o.kind != Kind::Stroke) { ++it; continue; }

			std::vector<std::vector<PointF>> runs;
			std::vector<PointF> cur;
			bool cut = false;
			for (const auto& lp : o.lp)
			{
				PointF w = LocalToWorld(o, lp);
				if (InEraser(p, radius, square, w, o.width * 0.5f))
				{
					cut = true;
					if (cur.size() >= 2) runs.push_back(cur);
					cur.clear();
				}
				else cur.push_back(w);
			}
			if (!cut) { ++it; continue; }
			if (cur.size() >= 2) runs.push_back(cur);

			for (auto& r : runs)
			{
				auto n = MakeStroke(r, o.color, o.width, o.alpha);
				added.push_back(std::move(n));
			}
			it = objs.erase(it);
		}
		for (auto& a : added) Add(std::move(a));
	}

	void Doc::EraseShapesAt(const PointF& p, float radius, bool square)
	{
		for (auto it = objs.begin(); it != objs.end();)
		{
			Obj& o = **it;
			if (o.kind == Kind::Stroke) { ++it; continue; }

			bool erase = ObjHit(o, p, radius * 0.6f);
			if (!erase)
			{
				std::vector<PointF> out;
				ObjOutline(o, out, 48);
				for (const auto& q : out)
				{
					if (InEraser(p, radius, square, q, o.width * 0.5f)) { erase = true; break; }
				}
			}
			if (erase) it = objs.erase(it);
			else ++it;
		}
	}

	void Doc::EraseAt(const PointF& p, float radius, bool square)
	{
		EraseStrokesAt(p, radius, square);
		EraseShapesAt(p, radius, square);
	}
}
