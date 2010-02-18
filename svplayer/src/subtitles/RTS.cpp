/* 
 *	Copyright (C) 2003-2006 Gabest
 *	http://www.gabest.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *   
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *   
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA. 
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "stdafx.h"
#include <math.h>
#include <time.h>
#include "RTS.h"

#include <afxtempl.h>
#include "..\apps\mplayerc\mplayerc.h"

#include "../svplib/SVPToolBox.h"
// WARNING: this isn't very thread safe, use only one RTS a time.
static HDC g_hDC;
static int g_hDC_refcnt = 0;

static long revcolor(long c)
{
	return ((c&0xff0000)>>16) + (c&0xff00) + ((c&0xff)<<16);
}

//////////////////////////////////////////////////////////////////////////////////////////////

// CMyFont

CMyFont::CMyFont(STSStyle& style)
{
	LOGFONT lf;
	memset(&lf, 0, sizeof(lf));
	lf <<= style;
	lf.lfHeight = (LONG)(style.fontSize+0.5);
	lf.lfOutPrecision = OUT_TT_PRECIS;
	lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
	lf.lfQuality = ANTIALIASED_QUALITY;
	lf.lfPitchAndFamily = DEFAULT_PITCH|FF_DONTCARE;

	if(!CreateFontIndirect(&lf))
	{
		_tcscpy(lf.lfFaceName, _T("Arial"));
		CreateFontIndirect(&lf);
	}

	HFONT hOldFont = SelectFont(g_hDC, *this);
	TEXTMETRIC tm;
	GetTextMetrics(g_hDC, &tm);
	m_ascent = ((tm.tmAscent + 4) >> 3);
	m_descent = ((tm.tmDescent + 4) >> 3);
	SelectFont(g_hDC, hOldFont);
}

// CWord

CWord::CWord(STSStyle& style, CStringW str, int ktype, int kstart, int kend) 
	: m_style(style), m_str(str)
	, m_width(0), m_ascent(0), m_descent(0)
	, m_ktype(ktype), m_kstart(kstart), m_kend(kend)
	, m_fDrawn(false), m_p(INT_MAX, INT_MAX)
	, m_fLineBreak(false), m_fWhiteSpaceChar(false)
	, m_pOpaqueBox(NULL)
{
	if(str.IsEmpty()) 
	{
		m_fWhiteSpaceChar = m_fLineBreak = true; 
	}

	CMyFont font(m_style);
	m_ascent = (int)(m_style.fontScaleY/100*font.m_ascent);
	m_descent = (int)(m_style.fontScaleY/100*font.m_descent);
	m_width = 0;
}

CWord::~CWord()
{
	if(m_pOpaqueBox) delete m_pOpaqueBox;
}

bool CWord::Append(CWord* w)
{
	if(!(m_style == w->m_style)
	|| m_fLineBreak || w->m_fLineBreak
	|| w->m_kstart != w->m_kend || m_ktype != w->m_ktype) return(false);

	m_fWhiteSpaceChar = m_fWhiteSpaceChar && w->m_fWhiteSpaceChar;
	m_str += w->m_str;
	m_width += w->m_width;

	m_fDrawn = false;
	m_p = CPoint(INT_MAX, INT_MAX);

	return(true);
}

void CWord::Paint(CPoint p, CPoint org)
{
	if(!m_str) return;
	if(m_str.Find(L"m 0 0 l") == 0) return; //some unknow thing from Apple Text Media Handler
	//SVP_LogMsg5(L"CWord::Paint %s",m_str);
	if(!m_fDrawn)
	{
		if(!CreatePath()) return;

		Transform(CPoint((org.x-p.x)*8, (org.y-p.y)*8));

		if(!ScanConvert()) return;

		if(m_style.borderStyle == 0 && (m_style.outlineWidthX+m_style.outlineWidthY > 0))
		{
			if(!CreateWidenedRegion((int)(m_style.outlineWidthX+0.5), (int)(m_style.outlineWidthY+0.5))) return;
		}
		else if(m_style.borderStyle == 1)
		{
			if(!CreateOpaqueBox()) return;
		}

		m_fDrawn = true;

		if(!Rasterize(p.x&7, p.y&7, m_style.fBlur, m_style.fGaussianBlur)) return;
	}
	else if((m_p.x&7) != (p.x&7) || (m_p.y&7) != (p.y&7))
	{
		Rasterize(p.x&7, p.y&7, m_style.fBlur, m_style.fGaussianBlur);
	}

	m_p = p;

	if(m_pOpaqueBox)
		m_pOpaqueBox->Paint(p, org);
}

void CWord::Transform(CPoint org)
{
	double scalex = m_style.fontScaleX/100;
	double scaley = m_style.fontScaleY/100;

	double caz = cos((3.1415/180)*m_style.fontAngleZ);
	double saz = sin((3.1415/180)*m_style.fontAngleZ);
	double cax = cos((3.1415/180)*m_style.fontAngleX);
	double sax = sin((3.1415/180)*m_style.fontAngleX);
	double cay = cos((3.1415/180)*m_style.fontAngleY);
	double say = sin((3.1415/180)*m_style.fontAngleY);

	for(int i = 0; i < mPathPoints; i++)
	{
		double x, y, z, xx, yy, zz;

		x = scalex * (mpPathPoints[i].x + m_style.fontShiftX * mpPathPoints[i].y) - org.x;
		y = scaley * (mpPathPoints[i].y + m_style.fontShiftY * mpPathPoints[i].x) - org.y;
		z = 0;

		xx = x*caz + y*saz;
		yy = -(x*saz - y*caz);
		zz = z;

		x = xx;
		y = yy*cax + zz*sax;
		z = yy*sax - zz*cax;

		xx = x*cay + z*say;
		yy = y;
		zz = x*say - z*cay;

		zz = max(zz, -19000);

		x = (xx * 20000) / (zz + 20000);
		y = (yy * 20000) / (zz + 20000);

		mpPathPoints[i].x = (LONG)(x + org.x + 0.5);
		mpPathPoints[i].y = (LONG)(y + org.y + 0.5);
	}
}

bool CWord::CreateOpaqueBox()
{
	if(m_pOpaqueBox) return(true);

	STSStyle style = m_style;
	style.borderStyle = 0;
	style.outlineWidthX = style.outlineWidthY = 0;
	style.colors[0] = m_style.colors[2];
	style.alpha[0] = m_style.alpha[2];

	int w = (int)(m_style.outlineWidthX + 0.5);
	int h = (int)(m_style.outlineWidthY + 0.5);

	CStringW str;
	str.Format(L"m %d %d l %d %d %d %d %d %d", 
		-w, -h, 
		m_width+w, -h, 
		m_width+w, m_ascent+m_descent+h, 
		-w, m_ascent+m_descent+h);

	m_pOpaqueBox = new CPolygon(style, str, 0, 0, 0, 1.0/8, 1.0/8, 0);

	return(!!m_pOpaqueBox);
}

// CText

CText::CText(STSStyle& style, CStringW str, int ktype, int kstart, int kend)
	: CWord(style, str, ktype, kstart, kend)
{
	if(m_str == L" ")
	{
		m_fWhiteSpaceChar = true;
	}

	CMyFont font(m_style);

	HFONT hOldFont = SelectFont(g_hDC, font);

	if(m_style.fontSpacing || (long)GetVersion() < 0)
	{
		bool bFirstPath = true;

		for(LPCWSTR s = m_str; *s; s++)
		{
			CSize extent;
			if(!GetTextExtentPoint32W(g_hDC, s, 1, &extent)) {SelectFont(g_hDC, hOldFont); ASSERT(0); return;}
			m_width += extent.cx + (int)m_style.fontSpacing;
		}
//			m_width -= (int)m_style.fontSpacing; // TODO: subtract only at the end of the line
	}
	else
	{
		CSize extent;
		if(!GetTextExtentPoint32W(g_hDC, m_str, wcslen(str), &extent)) {SelectFont(g_hDC, hOldFont); ASSERT(0); return;}
		m_width += extent.cx;
	}

	m_width = (int)(m_style.fontScaleX/100*m_width + 4) >> 3;

	SelectFont(g_hDC, hOldFont);
}

CWord* CText::Copy()
{
	return(new CText(m_style, m_str, m_ktype, m_kstart, m_kend));
}

bool CText::Append(CWord* w)
{
	return(dynamic_cast<CText*>(w) && CWord::Append(w));
}

bool CText::CreatePath()
{
	CMyFont font(m_style);

	HFONT hOldFont = SelectFont(g_hDC, font);

	int width = 0;

	if(m_style.fontSpacing || (long)GetVersion() < 0)
	{
		bool bFirstPath = true;

		for(LPCWSTR s = m_str; *s; s++)
		{
			CSize extent;
			if(!GetTextExtentPoint32W(g_hDC, s, 1, &extent)) {SelectFont(g_hDC, hOldFont); ASSERT(0); return(false);}

			PartialBeginPath(g_hDC, bFirstPath); bFirstPath = false;
			TextOutW(g_hDC, 0, 0, s, 1);
			PartialEndPath(g_hDC, width, 0);

			width += extent.cx + (int)m_style.fontSpacing;
		}
	}
	else
	{
		CSize extent;
		if(!GetTextExtentPoint32W(g_hDC, m_str, m_str.GetLength(), &extent)) {SelectFont(g_hDC, hOldFont); ASSERT(0); return(false);}

		BeginPath(g_hDC);
		TextOutW(g_hDC, 0, 0, m_str, m_str.GetLength());
		EndPath(g_hDC);
	}

	SelectFont(g_hDC, hOldFont);

	return(true);
}

// CPolygon

CPolygon::CPolygon(STSStyle& style, CStringW str, int ktype, int kstart, int kend, double scalex, double scaley, int baseline) 
	: CWord(style, str, ktype, kstart, kend)
	, m_scalex(scalex), m_scaley(scaley), m_baseline(baseline)
{
	ParseStr();
}

CPolygon::~CPolygon()
{
}

CWord* CPolygon::Copy()
{
	return(new CPolygon(m_style, m_str, m_ktype, m_kstart, m_kend, m_scalex, m_scaley, m_baseline));
}

bool CPolygon::Append(CWord* w)
{
	int width = m_width;

	CPolygon* p = dynamic_cast<CPolygon*>(w);
	if(!p) return(false);

	// TODO
	return(false);

	return(true);
}

bool CPolygon::GetLONG(CStringW& str, LONG& ret)
{
	LPWSTR s = (LPWSTR)(LPCWSTR)str, e = s;
	ret = wcstol(str, &e, 10);
	str = str.Mid(e - s);
	return(e > s);
}

bool CPolygon::GetPOINT(CStringW& str, POINT& ret)
{
	return(GetLONG(str, ret.x) && GetLONG(str, ret.y));
}

bool CPolygon::ParseStr()
{
	if(m_pathTypesOrg.GetCount() > 0) return(true);

	CPoint p;
	int i, j, lastsplinestart = -1, firstmoveto = -1, lastmoveto = -1;

	CStringW str = m_str;
	str.SpanIncluding(L"mnlbspc 0123456789");
	str.Replace(L"m", L"*m");
	str.Replace(L"n", L"*n");
	str.Replace(L"l", L"*l");
	str.Replace(L"b", L"*b");
	str.Replace(L"s", L"*s");
	str.Replace(L"p", L"*p");
	str.Replace(L"c", L"*c");

	int k = 0;
	for(CStringW s = str.Tokenize(L"*", k); !s.IsEmpty(); s = str.Tokenize(L"*", k))
	{
		WCHAR c = s[0];
		s.TrimLeft(L"mnlbspc ");
		switch(c)
		{
		case 'm': 
			lastmoveto = m_pathTypesOrg.GetCount();
			if(firstmoveto == -1) firstmoveto = lastmoveto;
			while(GetPOINT(s, p)) {m_pathTypesOrg.Add(PT_MOVETO); m_pathPointsOrg.Add(p);}
			break;
		case 'n':
			while(GetPOINT(s, p)) {m_pathTypesOrg.Add(PT_MOVETONC); m_pathPointsOrg.Add(p);}
			break;
		case 'l':
			while(GetPOINT(s, p)) {m_pathTypesOrg.Add(PT_LINETO); m_pathPointsOrg.Add(p);}
			break;
		case 'b':
			j = m_pathTypesOrg.GetCount();
			while(GetPOINT(s, p)) {m_pathTypesOrg.Add(PT_BEZIERTO); m_pathPointsOrg.Add(p); j++;}
			j = m_pathTypesOrg.GetCount() - ((m_pathTypesOrg.GetCount()-j)%3);
			m_pathTypesOrg.SetCount(j); m_pathPointsOrg.SetCount(j);
			break;
		case 's':
			j = lastsplinestart = m_pathTypesOrg.GetCount();
			i = 3;
			while(i-- && GetPOINT(s, p)) {m_pathTypesOrg.Add(PT_BSPLINETO); m_pathPointsOrg.Add(p); j++;}
			if(m_pathTypesOrg.GetCount()-lastsplinestart < 3) {m_pathTypesOrg.SetCount(lastsplinestart); m_pathPointsOrg.SetCount(lastsplinestart); lastsplinestart = -1;}
			// no break here
		case 'p':
			while(GetPOINT(s, p)) {m_pathTypesOrg.Add(PT_BSPLINEPATCHTO); m_pathPointsOrg.Add(p); j++;}
			break;
		case 'c':
			if(lastsplinestart > 0)
			{
				m_pathTypesOrg.Add(PT_BSPLINEPATCHTO);
				m_pathTypesOrg.Add(PT_BSPLINEPATCHTO);
				m_pathTypesOrg.Add(PT_BSPLINEPATCHTO);
				p = m_pathPointsOrg[lastsplinestart-1]; // we need p for temp storage, because operator [] will return a reference to CPoint and Add() may reallocate its internal buffer (this is true for MFC 7.0 but not for 6.0, hehe)
				m_pathPointsOrg.Add(p);
				p = m_pathPointsOrg[lastsplinestart];
				m_pathPointsOrg.Add(p);
				p = m_pathPointsOrg[lastsplinestart+1];
				m_pathPointsOrg.Add(p);
				lastsplinestart = -1;
			}
			break;
		default:
			break;
		}
	}
/*
	LPCWSTR str = m_str;
	while(*str)
	{
		while(*str && *str != 'm' && *str != 'n' && *str != 'l' && *str != 'b' && *str != 's' && *str != 'p' && *str != 'c') str++;

		if(!*str) break;

		switch(*str++)
		{
		case 'm': 
			lastmoveto = m_pathTypesOrg.GetCount();
			if(firstmoveto == -1) firstmoveto = lastmoveto;
			while(GetPOINT(str, p)) {m_pathTypesOrg.Add(PT_MOVETO); m_pathPointsOrg.Add(p);}
			break;
		case 'n':
			while(GetPOINT(str, p)) {m_pathTypesOrg.Add(PT_MOVETONC); m_pathPointsOrg.Add(p);}
			break;
		case 'l':
			while(GetPOINT(str, p)) {m_pathTypesOrg.Add(PT_LINETO); m_pathPointsOrg.Add(p);}
			break;
		case 'b':
			j = m_pathTypesOrg.GetCount();
			while(GetPOINT(str, p)) {m_pathTypesOrg.Add(PT_BEZIERTO); m_pathPointsOrg.Add(p); j++;}
			j = m_pathTypesOrg.GetCount() - ((m_pathTypesOrg.GetCount()-j)%3);
			m_pathTypesOrg.SetCount(j); m_pathPointsOrg.SetCount(j);
			break;
		case 's':
			j = lastsplinestart = m_pathTypesOrg.GetCount();
			i = 3;
			while(i-- && GetPOINT(str, p)) {m_pathTypesOrg.Add(PT_BSPLINETO); m_pathPointsOrg.Add(p); j++;}
			if(m_pathTypesOrg.GetCount()-lastsplinestart < 3) {m_pathTypesOrg.SetCount(lastsplinestart); m_pathPointsOrg.SetCount(lastsplinestart); lastsplinestart = -1;}
			// no break here
		case 'p':
			while(GetPOINT(str, p)) {m_pathTypesOrg.Add(PT_BSPLINEPATCHTO); m_pathPointsOrg.Add(p); j++;}
			break;
		case 'c':
			if(lastsplinestart > 0)
			{
				m_pathTypesOrg.Add(PT_BSPLINEPATCHTO);
				m_pathTypesOrg.Add(PT_BSPLINEPATCHTO);
				m_pathTypesOrg.Add(PT_BSPLINEPATCHTO);
				p = m_pathPointsOrg[lastsplinestart-1]; // we need p for temp storage, because operator [] will return a reference to CPoint and Add() may reallocate its internal buffer (this is true for MFC 7.0 but not for 6.0, hehe)
				m_pathPointsOrg.Add(p);
				p = m_pathPointsOrg[lastsplinestart];
				m_pathPointsOrg.Add(p);
				p = m_pathPointsOrg[lastsplinestart+1];
				m_pathPointsOrg.Add(p);
				lastsplinestart = -1;
			}
			break;
		default:
			break;
		}

		if(firstmoveto > 0) break;
	}
*/
	if(lastmoveto == -1 || firstmoveto > 0) 
	{
		m_pathTypesOrg.RemoveAll();
		m_pathPointsOrg.RemoveAll();
		return(false);
	}

	int minx = INT_MAX, miny = INT_MAX, maxx = -INT_MAX, maxy = -INT_MAX;

	for(i = 0; i < m_pathTypesOrg.GetCount(); i++)
	{
		m_pathPointsOrg[i].x = (int)(64 * m_scalex * m_pathPointsOrg[i].x);
		m_pathPointsOrg[i].y = (int)(64 * m_scaley * m_pathPointsOrg[i].y);
		if(minx > m_pathPointsOrg[i].x) minx = m_pathPointsOrg[i].x;
		if(miny > m_pathPointsOrg[i].y) miny = m_pathPointsOrg[i].y;
		if(maxx < m_pathPointsOrg[i].x) maxx = m_pathPointsOrg[i].x;
		if(maxy < m_pathPointsOrg[i].y) maxy = m_pathPointsOrg[i].y;
	}

	m_width = max(maxx - minx, 0);
	m_ascent = max(maxy - miny, 0);

	int baseline = (int)(64 * m_scaley * m_baseline);
	m_descent = baseline;
	m_ascent -= baseline;

	m_width = ((int)(m_style.fontScaleX/100 * m_width) + 4) >> 3;
	m_ascent = ((int)(m_style.fontScaleY/100 * m_ascent) + 4) >> 3;
	m_descent = ((int)(m_style.fontScaleY/100 * m_descent) + 4) >> 3;

	return(true);
}

bool CPolygon::CreatePath()
{
	int len = m_pathTypesOrg.GetCount();
	if(len == 0) return(false);

	if(mPathPoints != len)
	{
		mpPathTypes = (BYTE*)realloc(mpPathTypes, len*sizeof(BYTE));
		mpPathPoints = (POINT*)realloc(mpPathPoints, len*sizeof(POINT));
		if(!mpPathTypes || !mpPathPoints) return(false);
		mPathPoints = len;
	}

	memcpy(mpPathTypes, m_pathTypesOrg.GetData(), len*sizeof(BYTE));
	memcpy(mpPathPoints, m_pathPointsOrg.GetData(), len*sizeof(POINT));

	return(true);
}

// CClipper

CClipper::CClipper(CStringW str, CSize size, double scalex, double scaley, bool inverse) 
	: CPolygon(STSStyle(), str, 0, 0, 0, scalex, scaley, 0)
{
	m_size.cx = m_size.cy = 0;
	m_pAlphaMask = NULL;

	if(size.cx < 0 || size.cy < 0 || !(m_pAlphaMask = new BYTE[size.cx*size.cy])) return;

	m_size = size;
	m_inverse = inverse;

	memset(m_pAlphaMask, 0, size.cx*size.cy);

	Paint(CPoint(0, 0), CPoint(0, 0));

	int w = mOverlayWidth, h = mOverlayHeight;

	int x = (mOffsetX+4)>>3, y = (mOffsetY+4)>>3;
	int xo = 0, yo = 0;

	if(x < 0) {xo = -x; w -= -x; x = 0;}
	if(y < 0) {yo = -y; h -= -y; y = 0;}
	if(x+w > m_size.cx) w = m_size.cx-x;
	if(y+h > m_size.cy) h = m_size.cy-y;

	if(w <= 0 || h <= 0) return;

	const BYTE* src = mpOverlayBuffer + 2*(mOverlayWidth * yo + xo);
	BYTE* dst = m_pAlphaMask + m_size.cx * y + x;

	while(h--)
	{
		for(int wt=0; wt<w; ++wt)
			dst[wt] = src[wt*2];

		src += 2*mOverlayWidth;
		dst += m_size.cx;
	}

	if(inverse)
	{
		BYTE* dst = m_pAlphaMask;
		for(int i = size.cx*size.cy; i>0; --i, ++dst)
			*dst = 0x40 - *dst; // mask is 6 bit
	}
}

CClipper::~CClipper()
{
	if(m_pAlphaMask) delete [] m_pAlphaMask;
	m_pAlphaMask = NULL;
}

CWord* CClipper::Copy()
{
	return(new CClipper(m_str, m_size, m_scalex, m_scaley, m_inverse));
}

bool CClipper::Append(CWord* w)
{
	return(false);
}

// CLine

CLine::~CLine()
{
	POSITION pos = GetHeadPosition();
	while(pos) delete GetNext(pos);
}

void CLine::Compact()
{
	POSITION pos = GetHeadPosition();
	while(pos)
	{
		CWord* w = GetNext(pos);
		if(!w->m_fWhiteSpaceChar) break;

		m_width -= w->m_width;
		delete w;
		RemoveHead();
	}

	pos = GetTailPosition();
	while(pos)
	{
		CWord* w = GetPrev(pos);
		if(!w->m_fWhiteSpaceChar) break;

		m_width -= w->m_width;
		delete w;
		RemoveTail();
	}

	if(IsEmpty()) return;

	CLine l;
	l.AddTailList(this);
	RemoveAll();

	CWord* last = NULL;

	pos = l.GetHeadPosition();
	while(pos)
	{
		CWord* w = l.GetNext(pos);

		if(!last || !last->Append(w))
			AddTail(last = w->Copy());
	}

	m_ascent = m_descent = m_borderX = m_borderY = 0;

	pos = GetHeadPosition();
	while(pos)
	{
		CWord* w = GetNext(pos);

		if(m_ascent < w->m_ascent) m_ascent = w->m_ascent;
		if(m_descent < w->m_descent) m_descent = w->m_descent;
		if(m_borderX < w->m_style.outlineWidthX) m_borderX = (int)(w->m_style.outlineWidthX+0.5);
		if(m_borderY < w->m_style.outlineWidthY) m_borderY = (int)(w->m_style.outlineWidthY+0.5);
	}
}

CRect CLine::PaintShadow(SubPicDesc& spd, CRect& clipRect, BYTE* pAlphaMask, CPoint p, CPoint org, int time, int alpha)
{
	CRect bbox(0, 0, 0, 0);

	POSITION pos = GetHeadPosition();
	while(pos)
	{
		CWord* w = GetNext(pos);

		if(w->m_fLineBreak) return(bbox); // should not happen since this class is just a line of text without any breaks

		if(w->m_style.shadowDepthX != 0 || w->m_style.shadowDepthY != 0)
		{
			int x = p.x + (int)(w->m_style.shadowDepthX+0.5);
			int y = p.y + m_ascent - w->m_ascent + (int)(w->m_style.shadowDepthY+0.5);

			DWORD a = 0xff - w->m_style.alpha[3];
			if(alpha > 0) a = MulDiv(a, 0xff - alpha, 0xff);
			COLORREF shadow = revcolor(w->m_style.colors[3]) | (a<<24);
			long sw[6] = {shadow, -1};

			w->Paint(CPoint(x, y), org);

			if(w->m_style.borderStyle == 0)
			{
				bbox |= w->Draw(spd, clipRect, pAlphaMask, x, y, sw, 
					w->m_ktype > 0 || w->m_style.alpha[0] < 0xff, 
					(w->m_style.outlineWidthX+w->m_style.outlineWidthY > 0) && !(w->m_ktype == 2 && time < w->m_kstart));
			}
			else if(w->m_style.borderStyle == 1 && w->m_pOpaqueBox)
			{
				bbox |= w->m_pOpaqueBox->Draw(spd, clipRect, pAlphaMask, x, y, sw, true, false);
			}
		}

		p.x += w->m_width;
	}

	return(bbox);
}

CRect CLine::PaintOutline(SubPicDesc& spd, CRect& clipRect, BYTE* pAlphaMask, CPoint p, CPoint org, int time, int alpha)
{
	CRect bbox(0, 0, 0, 0);

	POSITION pos = GetHeadPosition();
	while(pos)
	{
		CWord* w = GetNext(pos);

		if(w->m_fLineBreak) return(bbox); // should not happen since this class is just a line of text without any breaks

		if(w->m_style.outlineWidthX+w->m_style.outlineWidthY > 0 && !(w->m_ktype == 2 && time < w->m_kstart))
		{
			int x = p.x;
			int y = p.y + m_ascent - w->m_ascent;

			DWORD aoutline = w->m_style.alpha[2];
			if(alpha > 0) aoutline += MulDiv(alpha, 0xff - w->m_style.alpha[2], 0xff);
			COLORREF outline = revcolor(w->m_style.colors[2]) | ((0xff-aoutline)<<24);
			long sw[6] = {outline, -1};

			w->Paint(CPoint(x, y), org);

			if(w->m_style.borderStyle == 0)
			{
				bbox |= w->Draw(spd, clipRect, pAlphaMask, x, y, sw, !w->m_style.alpha[0] && !w->m_style.alpha[1] && !alpha, true);
			}
			else if(w->m_style.borderStyle == 1 && w->m_pOpaqueBox)
			{
				bbox |= w->m_pOpaqueBox->Draw(spd, clipRect, pAlphaMask, x, y, sw, true, false);
			}
		}

		p.x += w->m_width;
	}

	return(bbox);
}

CRect CLine::PaintBody(SubPicDesc& spd, CRect& clipRect, BYTE* pAlphaMask, CPoint p, CPoint org, int time, int alpha)
{
	CRect bbox(0, 0, 0, 0);

	POSITION pos = GetHeadPosition();
	while(pos)
	{
		CWord* w = GetNext(pos);

		if(w->m_fLineBreak) return(bbox); // should not happen since this class is just a line of text without any breaks

		int x = p.x;
		int y = p.y + m_ascent - w->m_ascent;

		// colors

		DWORD aprimary = w->m_style.alpha[0];
		if(alpha > 0) aprimary += MulDiv(alpha, 0xff - w->m_style.alpha[0], 0xff);
		COLORREF primary = revcolor(w->m_style.colors[0]) | ((0xff-aprimary)<<24);

		DWORD asecondary = w->m_style.alpha[1];
		if(alpha > 0) asecondary += MulDiv(alpha, 0xff - w->m_style.alpha[1], 0xff);
		COLORREF secondary = revcolor(w->m_style.colors[1]) | ((0xff-asecondary)<<24);

		long sw[6] = {primary, 0, secondary};

		// karaoke

		double t;

		if(w->m_ktype == 0 || w->m_ktype == 2)
		{
			t = time < w->m_kstart ? 0 : 1;
		}
		else if(w->m_ktype == 1)
		{
			if(time < w->m_kstart) t = 0;
			else if(time < w->m_kend) 
			{
				t = 1.0 * (time - w->m_kstart) / (w->m_kend - w->m_kstart);

				double angle = fmod(w->m_style.fontAngleZ, 360.0);
				if(angle > 90 && angle < 270) 
				{
					t = 1-t;
					COLORREF tmp = sw[0]; sw[0] = sw[2]; sw[2] = tmp;
				}
			}
			else t = 1.0;
		}

		if(t >= 1)
		{
			sw[1] = 0xffffffff;
		}

		sw[3] = (int)(w->m_style.outlineWidthX + t*w->m_width) >> 3;
		sw[4] = sw[2];
		sw[5] = 0x00ffffff;

		w->Paint(CPoint(x, y), org);

		bbox |= w->Draw(spd, clipRect, pAlphaMask, x, y, sw, true, false);

		p.x += w->m_width;
	}

	return(bbox);
}


// CSubtitle

CSubtitle::CSubtitle()
{
	memset(m_effects, 0, sizeof(Effect*)*EF_NUMBEROFEFFECTS);
	m_pClipper = NULL;
	m_clipInverse = false;
	m_scalex = m_scaley = 1;
}

CSubtitle::~CSubtitle()
{
	Empty();
}

void CSubtitle::Empty()
{
	POSITION pos = GetHeadPosition();
	while(pos) delete GetNext(pos);

	pos = m_words.GetHeadPosition();
	while(pos) delete m_words.GetNext(pos);

	for(int i = 0; i < EF_NUMBEROFEFFECTS; i++) {if(m_effects[i]) delete m_effects[i];}
	memset(m_effects, 0, sizeof(Effect*)*EF_NUMBEROFEFFECTS);

	if(m_pClipper) delete m_pClipper;
	m_pClipper = NULL;
}

int CSubtitle::GetFullWidth()
{
	int width = 0;

	POSITION pos = m_words.GetHeadPosition();
	while(pos) width += m_words.GetNext(pos)->m_width;

	return(width);
}

int CSubtitle::GetFullLineWidth(POSITION pos)
{
	int width = 0;

	while(pos) 
	{
		CWord* w = m_words.GetNext(pos);
		if(w->m_fLineBreak) break;
		width += w->m_width;
	}

	return(width);
}

int CSubtitle::GetWrapWidth(POSITION pos, int maxwidth)
{
	if(m_wrapStyle == 0 || m_wrapStyle == 3)
	{
		if(maxwidth > 0) 
		{
//			int fullwidth = GetFullWidth();
			int fullwidth = GetFullLineWidth(pos);

			int minwidth = fullwidth / ((abs(fullwidth) / maxwidth) + 1);

			int width = 0, wordwidth = 0;

			while(pos && width < minwidth)
			{
				CWord* w = m_words.GetNext(pos);
				wordwidth = w->m_width;
				if(abs(width + wordwidth) < abs(maxwidth)) width += wordwidth;
			}

			maxwidth = width;

			if(m_wrapStyle == 3 && pos) maxwidth -= wordwidth;
		}
	}
	else if(m_wrapStyle == 1)
	{
//		maxwidth = maxwidth;
	}
	else if(m_wrapStyle == 2)
	{
		maxwidth = INT_MAX;
	}

	return(maxwidth);
}

CLine* CSubtitle::GetNextLine(POSITION& pos, int maxwidth)
{
	if(pos == NULL) return(NULL);

	CLine* ret = new CLine();
	if(!ret) return(NULL);

	ret->m_width = ret->m_ascent = ret->m_descent = ret->m_borderX = ret->m_borderY = 0;

	maxwidth = GetWrapWidth(pos, maxwidth);

	bool fEmptyLine = true;

	while(pos)
	{
		CWord* w = m_words.GetNext(pos);

		if(ret->m_ascent < w->m_ascent) ret->m_ascent = w->m_ascent;
		if(ret->m_descent < w->m_descent) ret->m_descent = w->m_descent;
		if(ret->m_borderX < w->m_style.outlineWidthX) ret->m_borderX = (int)(w->m_style.outlineWidthX+0.5);
		if(ret->m_borderY < w->m_style.outlineWidthY) ret->m_borderY = (int)(w->m_style.outlineWidthY+0.5);

		if(w->m_fLineBreak) 
		{
			if(fEmptyLine) {ret->m_ascent /= 2; ret->m_descent /= 2; ret->m_borderX = ret->m_borderY = 0;}

			ret->Compact();

			return(ret);
		}

		fEmptyLine = false;

		bool fWSC = w->m_fWhiteSpaceChar;

		int width = w->m_width;
		POSITION pos2 = pos;
		while(pos2)
		{
			if(m_words.GetAt(pos2)->m_fWhiteSpaceChar != fWSC
			|| m_words.GetAt(pos2)->m_fLineBreak) break;

			CWord* w2 = m_words.GetNext(pos2);
			width += w2->m_width;
		}

		if((ret->m_width += width) <= maxwidth || ret->IsEmpty()) 
		{
			ret->AddTail(w->Copy());
			
			while(pos != pos2)
			{
				ret->AddTail(m_words.GetNext(pos)->Copy());
			}

			pos = pos2;
		}
		else
		{
			if(pos) m_words.GetPrev(pos);
			else pos = m_words.GetTailPosition();

			ret->m_width -= width;

			break;
		}
	}

	ret->Compact();

	return(ret);
}

void CSubtitle::CreateClippers(CSize size)
{
	size.cx >>= 3;
	size.cy >>= 3;

	if(m_effects[EF_BANNER] && m_effects[EF_BANNER]->param[2])
	{
		int width = m_effects[EF_BANNER]->param[2];

		int w = size.cx, h = size.cy;

		if(!m_pClipper) 
		{
			CStringW str;
			str.Format(L"m %d %d l %d %d %d %d %d %d", 0, 0, w, 0, w, h, 0, h);
			m_pClipper = new CClipper(str, size, 1, 1, false);
			if(!m_pClipper) return;
		}

		int da = (64<<8)/width;
		BYTE* am = m_pClipper->m_pAlphaMask;

		for(int j = 0; j < h; j++, am += w)
		{
			int a = 0;
			int k = min(width, w);
			
			for(int i = 0; i < k; i++, a += da)
				am[i] = (am[i]*a)>>14;

			a = 0x40<<8;
			k = w-width;

			if(k < 0) {a -= -k*da; k = 0;}
            
			for(int i = k; i < w; i++, a -= da)
				am[i] = (am[i]*a)>>14;
		}
	}
	else if(m_effects[EF_SCROLL] && m_effects[EF_SCROLL]->param[4])
	{
		int height = m_effects[EF_SCROLL]->param[4];

		int w = size.cx, h = size.cy;

		if(!m_pClipper) 
		{
			CStringW str;
			str.Format(L"m %d %d l %d %d %d %d %d %d", 0, 0, w, 0, w, h, 0, h);
			m_pClipper = new CClipper(str, size, 1, 1, false);
			if(!m_pClipper) return;
		}

		int da = (64<<8)/height;
		int a = 0;
		int k = m_effects[EF_SCROLL]->param[0]>>3;
		int l = k+height;
		if(k < 0) {a += -k*da; k = 0;}
		if(l > h) {l = h;}

		if(k < h)
		{
			BYTE* am = &m_pClipper->m_pAlphaMask[k*w];

			memset(m_pClipper->m_pAlphaMask, 0, am - m_pClipper->m_pAlphaMask);

			for(int j = k; j < l; j++, a += da)
			{
				for(int i = 0; i < w; i++, am++)
					*am = ((*am)*a)>>14;
			}
		}

		da = -(64<<8)/height;
		a = 0x40<<8;
		l = m_effects[EF_SCROLL]->param[1]>>3;
		k = l-height;
		if(k < 0) {a += -k*da; k = 0;}
		if(l > h) {l = h;}

		if(k < h)
		{
			BYTE* am = &m_pClipper->m_pAlphaMask[k*w];

			int j = k;
			for(; j < l; j++, a += da)
			{
				for(int i = 0; i < w; i++, am++)
					*am = ((*am)*a)>>14;
			}

			memset(am, 0, (h-j)*w);
		}
	}
}

void CSubtitle::MakeLines(CSize size, CRect marginRect)
{
	CSize spaceNeeded(0, 0);

	bool fFirstLine = true;

	m_topborder = m_bottomborder = 0;

	CLine* l = NULL;
	
	POSITION pos = m_words.GetHeadPosition();
	while(pos)
	{
		l = GetNextLine(pos, size.cx - marginRect.left - marginRect.right);
		if(!l) break;

		if(fFirstLine) {m_topborder = l->m_borderY; fFirstLine = false;}

		spaceNeeded.cx = max(l->m_width+l->m_borderX, spaceNeeded.cx);
		spaceNeeded.cy += l->m_ascent + l->m_descent;

		AddTail(l);
	}

	if(l) m_bottomborder = l->m_borderY;

	m_rect = CRect(
		CPoint((m_scrAlignment%3) == 1 ? marginRect.left
				: (m_scrAlignment%3) == 2 ? (marginRect.left + (size.cx - marginRect.right) - spaceNeeded.cx + 1) / 2
				: (size.cx - marginRect.right - spaceNeeded.cx),
				m_scrAlignment <= 3 ? (size.cy - marginRect.bottom - spaceNeeded.cy)
				: m_scrAlignment <= 6 ? (marginRect.top + (size.cy - marginRect.bottom) - spaceNeeded.cy + 1) / 2
				: marginRect.top),
		spaceNeeded);
}

// CScreenLayoutAllocator

void CScreenLayoutAllocator::Empty()
{
	m_subrects.RemoveAll();
}

void CScreenLayoutAllocator::AdvanceToSegment(int segment, const CAtlArray<int>& sa)
{
	POSITION pos = m_subrects.GetHeadPosition();
	while(pos)
	{
		POSITION prev = pos;

		SubRect& sr = m_subrects.GetNext(pos);

		bool fFound = false;

		if(abs(sr.segment - segment) <= 1) // using abs() makes it possible to play the subs backwards, too :)
		{
			for(int i = 0; i < sa.GetCount() && !fFound; i++)
			{
				if(sa[i] == sr.entry) 
				{
					sr.segment = segment;
					fFound = true;
				}
			}
		}

		if(!fFound) m_subrects.RemoveAt(prev);
	}
}

CRect CScreenLayoutAllocator::AllocRect(CSubtitle* s, int segment, int entry, int layer, int collisions)
{
	// TODO: handle collisions == 1 (reversed collisions)

	POSITION pos = m_subrects.GetHeadPosition();
	while(pos)
	{
		SubRect& sr = m_subrects.GetNext(pos);
		if(sr.segment == segment && sr.entry == entry) 
		{
			return(sr.r + CRect(0, -s->m_topborder, 0, -s->m_bottomborder));
		}
	}

	CRect r = s->m_rect + CRect(0, s->m_topborder, 0, s->m_bottomborder);

	bool fSearchDown = s->m_scrAlignment > 3;

	bool fOK;

	do
	{
		fOK = true;

		pos = m_subrects.GetHeadPosition();
		while(pos)
		{
			SubRect& sr = m_subrects.GetNext(pos);

			if(layer == sr.layer && !(r & sr.r).IsRectEmpty())
			{
				if(fSearchDown)
				{
					r.bottom = sr.r.bottom + r.Height();
					r.top = sr.r.bottom;
				}
				else
				{
					r.top = sr.r.top - r.Height();
					r.bottom = sr.r.top;
				}
				
				fOK = false;
			}
		}
	}
	while(!fOK);

	SubRect sr;
	sr.r = r;
	sr.segment = segment;
	sr.entry = entry;
	sr.layer = layer;
	m_subrects.AddTail(sr);
	
	return(sr.r + CRect(0, -s->m_topborder, 0, -s->m_bottomborder));
}

// CRenderedTextSubtitle

CRenderedTextSubtitle::CRenderedTextSubtitle(CCritSec* pLock)
	: ISubPicProviderImpl(pLock)
{
	m_size = CSize(0, 0);

	if(g_hDC_refcnt == 0) 
	{
		g_hDC = CreateCompatibleDC(NULL);
		SetBkMode(g_hDC, TRANSPARENT); 
		SetTextColor(g_hDC, 0xffffff); 
		SetMapMode(g_hDC, MM_TEXT);
	}

	g_hDC_refcnt++;

	sub_delay_ms = 0;
	notSaveDelay = 0;
}

CRenderedTextSubtitle::~CRenderedTextSubtitle()
{
	Deinit();

	g_hDC_refcnt--;
	if(g_hDC_refcnt == 0) DeleteDC(g_hDC);
}

void CRenderedTextSubtitle::Copy(CSimpleTextSubtitle& sts)
{
	__super::Copy(sts);

	m_size = CSize(0, 0);

	if(CRenderedTextSubtitle* pRTS = dynamic_cast<CRenderedTextSubtitle*>(&sts))
	{
		m_size = pRTS->m_size;
	}
}

void CRenderedTextSubtitle::Empty()
{
	Deinit();

	__super::Empty();
}

void CRenderedTextSubtitle::OnChanged()
{
	__super::OnChanged();

	POSITION pos = m_subtitleCache.GetStartPosition();
	while(pos)
	{
		int i;
		CSubtitle* s;
        m_subtitleCache.GetNextAssoc(pos, i, s);
		delete s;
	}

	m_subtitleCache.RemoveAll();

	m_sla.Empty();
}

bool CRenderedTextSubtitle::Init(CSize size, CRect vidrect)
{
	Deinit();

	m_size = CSize(size.cx*8, size.cy*8);
	m_vidrect = CRect(vidrect.left*8, vidrect.top*8, vidrect.right*8, vidrect.bottom*8);

	m_sla.Empty();

	return(true);
}

void CRenderedTextSubtitle::Deinit()
{
	POSITION pos = m_subtitleCache.GetStartPosition();
	while(pos)
	{
		int i;
		CSubtitle* s;
        m_subtitleCache.GetNextAssoc(pos, i, s);
		delete s;
	}

	m_subtitleCache.RemoveAll();

	m_sla.Empty();

	m_size = CSize(0, 0);
	m_vidrect.SetRectEmpty();
}

void CRenderedTextSubtitle::ParseEffect(CSubtitle* sub, CString str)
{
	str.Trim();
	if(!sub || str.IsEmpty()) return;

	const TCHAR* s = _tcschr(str, ';');
	if(!s) {s = (LPTSTR)(LPCTSTR)str; s += str.GetLength()-1;}
	s++;
	CString effect = str.Left(s - str);

	if(!effect.CompareNoCase(_T("Banner;")))
	{
		int delay, lefttoright = 0, fadeawaywidth = 0;
		if(_stscanf(s, _T("%d;%d;%d"), &delay, &lefttoright, &fadeawaywidth) < 1) return;

		Effect* e = new Effect;
		if(!e) return;

		sub->m_effects[e->type = EF_BANNER] = e;
		e->param[0] = (int)(max(1.0*delay/sub->m_scalex, 1));
		e->param[1] = lefttoright;
		e->param[2] = (int)(sub->m_scalex*fadeawaywidth);

		sub->m_wrapStyle = 2;
	}
	else if(!effect.CompareNoCase(_T("Scroll up;")) || !effect.CompareNoCase(_T("Scroll down;")))
	{
		int top, bottom, delay, fadeawayheight = 0;
		if(_stscanf(s, _T("%d;%d;%d;%d"), &top, &bottom, &delay, &fadeawayheight) < 3) return;

		if(top > bottom) {int tmp = top; top = bottom; bottom = tmp;}

		Effect* e = new Effect;
		if(!e) return;

		sub->m_effects[e->type = EF_SCROLL] = e;
		e->param[0] = (int)(sub->m_scaley*top*8);
		e->param[1] = (int)(sub->m_scaley*bottom*8);
		e->param[2] = (int)(max(1.0*delay/sub->m_scaley, 1));
		e->param[3] = (effect.GetLength() == 12);
		e->param[4] = (int)(sub->m_scaley*fadeawayheight);
	}
}



//Thesaurus source:<a href="http://meta.wikimedia.org/wiki/Automatic_conversion_between_simplified_and_traditional_Chinese">WIKIMEDIA</a><br />
//by wmr89502270@gmail.com
//TODO this should move to seperated lib
static WCHAR* gb2big2[] = { L"㑩", L"儸",L"㓥", L"劏",L"㔉", L"劚",L"㖊", L"噚",L"㖞", L"喎",L"㛟", L"𡞵",L"㛠", L"𡢃",L"㛿", L"𡠹",L"㟆", L"㠏",L"㧑", L"撝",L"㧟", L"擓",L"㨫", L"㩜",L"㱩", L"殰",L"㱮", L"殨",L"㲿", L"瀇",L"㳠", L"澾",L"㶉", L"鸂",L"㶶", L"燶",L"㶽", L"煱",L"㺍", L"獱",L"㻏", L"𤫩",L"㻘", L"𤪺",L"䁖", L"瞜",L"䅉", L"稏",L"䇲", L"筴",L"䌶", L"䊷",L"䌷", L"紬",L"䌸", L"縳",L"䌹", L"絅",L"䌺", L"䋙",L"䌼", L"綐",L"䌽", L"綵",L"䌾", L"䋻",L"䍀", L"繿",L"䍁", L"繸",L"䑽", L"𦪙",L"䓕", L"薳",L"䗖", L"螮",L"䘛", L"𧝞",L"䙊", L"𧜵",L"䙓", L"襬",L"䜣", L"訢",L"䜥", L"𧩙",L"䜧", L"譅",L"䝙", L"貙",L"䞌", L"𧵳",L"䞍", L"䝼",L"䞐", L"賰",L"䢂", L"𨋢",L"䥺", L"釾",L"䥽", L"鏺",L"䥿", L"𨯅",L"䦀", L"𨦫",L"䦁", L"𨧜",L"䦃", L"鐯",L"䦅", L"鐥",L"䩄", L"靦",L"䭪", L"𩞯",L"䯃", L"𩣑",L"䯄", L"騧",L"䯅", L"䯀",L"䲝", L"䱽",L"䲞", L"𩶘",L"䲟", L"鮣",L"䲠", L"鰆",L"䲡", L"鰌",L"䲢", L"鰧",L"䲣", L"䱷",L"䴓", L"鳾",L"䴔", L"鵁",L"䴕", L"鴷",L"䴖", L"鶄",L"䴗", L"鶪",L"䴘", L"鷈",L"䴙", L"鷿",L"万", L"萬",L"与", L"與",L"专", L"專",L"业", L"業",L"丛", L"叢",L"东", L"東",L"丝", L"絲",L"丢", L"丟",L"两", L"兩",L"严", L"嚴",L"丧", L"喪",L"个", L"個",L"丰", L"豐",L"临", L"臨",L"为", L"為",L"丽", L"麗",L"举", L"舉",L"义", L"義",L"乌", L"烏",L"乐", L"樂",L"乔", L"喬",L"习", L"習",L"乡", L"鄉",L"书", L"書",L"买", L"買",L"乱", L"亂",L"争", L"爭",L"于", L"於",L"亏", L"虧",L"云", L"雲",L"亚", L"亞",L"产", L"產",L"亩", L"畝",L"亲", L"親",L"亵", L"褻",L"亸", L"嚲",L"亿", L"億",L"仅", L"僅",L"从", L"從",L"仑", L"侖",L"仓", L"倉",L"仪", L"儀",L"们", L"們",L"价", L"價",L"众", L"眾",L"优", L"優",L"会", L"會",L"伛", L"傴",L"伞", L"傘",L"伟", L"偉",L"传", L"傳",L"伣", L"俔",L"伤", L"傷",L"伥", L"倀",L"伦", L"倫",L"伧", L"傖",L"伪", L"偽",L"伫", L"佇",L"体", L"體",L"佥", L"僉",L"侠", L"俠",L"侣", L"侶",L"侥", L"僥",L"侦", L"偵",L"侧", L"側",L"侨", L"僑",L"侩", L"儈",L"侪", L"儕",L"侬", L"儂",L"俣", L"俁",L"俦", L"儔",L"俨", L"儼",L"俩", L"倆",L"俪", L"儷",L"俫", L"倈",L"俭", L"儉",L"债", L"債",L"倾", L"傾",L"偬", L"傯",L"偻", L"僂",L"偾", L"僨",L"偿", L"償",L"傥", L"儻",L"傧", L"儐",L"储", L"儲",L"傩", L"儺",L"儿", L"兒",L"兑", L"兌",L"兖", L"兗",L"党", L"黨",L"兰", L"蘭",L"关", L"關",L"兴", L"興",L"兹", L"茲",L"养", L"養",L"兽", L"獸",L"冁", L"囅",L"内", L"內",L"冈", L"岡",L"册", L"冊",L"写", L"寫",L"军", L"軍",L"农", L"農",L"冯", L"馮",L"冲", L"沖",L"决", L"決",L"况", L"況",L"冻", L"凍",L"净", L"凈",L"凉", L"涼",L"减", L"減",L"凑", L"湊",L"凛", L"凜",L"几", L"幾",L"凤", L"鳳",L"凫", L"鳧",L"凭", L"憑",L"凯", L"凱",L"击", L"擊",L"凿", L"鑿",L"刍", L"芻",L"刘", L"劉",L"则", L"則",L"刚", L"剛",L"创", L"創",L"删", L"刪",L"别", L"別",L"刬", L"剗",L"刭", L"剄",L"刹", L"剎",L"刽", L"劊",L"刿", L"劌",L"剀", L"剴",L"剂", L"劑",L"剐", L"剮",L"剑", L"劍",L"剥", L"剝",L"剧", L"劇",L"劝", L"勸",L"办", L"辦",L"务", L"務",L"劢", L"勱",L"动", L"動",L"励", L"勵",L"劲", L"勁",L"劳", L"勞",L"势", L"勢",L"勋", L"勛",L"勚", L"勩",L"匀", L"勻",L"匦", L"匭",L"匮", L"匱",L"区", L"區",L"医", L"醫",L"华", L"華",L"协", L"協",L"单", L"單",L"卖", L"賣",L"卢", L"盧",L"卤", L"鹵",L"卫", L"衛",L"却", L"卻",L"卺", L"巹",L"厂", L"廠",L"厅", L"廳",L"历", L"歷",L"厉", L"厲",L"压", L"壓",L"厌", L"厭",L"厍", L"厙",L"厐", L"龎",L"厕", L"廁",L"厢", L"廂",L"厣", L"厴",L"厦", L"廈",L"厨", L"廚",L"厩", L"廄",L"厮", L"廝",L"县", L"縣",L"叁", L"叄",L"参", L"參",L"双", L"雙",L"发", L"發",L"变", L"變",L"叙", L"敘",L"叠", L"疊",L"叶", L"葉",L"号", L"號",L"叹", L"嘆",L"叽", L"嘰",L"吓", L"嚇",L"吕", L"呂",L"吗", L"嗎",L"吣", L"唚",L"吨", L"噸",L"听", L"聽",L"启", L"啟",L"吴", L"吳",L"呐", L"吶",L"呒", L"嘸",L"呓", L"囈",L"呕", L"嘔",L"呖", L"嚦",L"呗", L"唄",L"员", L"員",L"呙", L"咼",L"呛", L"嗆",L"呜", L"嗚",L"咏", L"詠",L"咙", L"嚨",L"咛", L"嚀",L"咝", L"噝",L"咤", L"吒",L"响", L"響",L"哑", L"啞",L"哒", L"噠",L"哓", L"嘵",L"哔", L"嗶",L"哕", L"噦",L"哗", L"嘩",L"哙", L"噲",L"哜", L"嚌",L"哝", L"噥",L"哟", L"喲",L"唛", L"嘜",L"唝", L"嗊",L"唠", L"嘮",L"唡", L"啢",L"唢", L"嗩",L"唤", L"喚",L"啧", L"嘖",L"啬", L"嗇",L"啭", L"囀",L"啮", L"嚙",L"啴", L"嘽",L"啸", L"嘯",L"喷", L"噴",L"喽", L"嘍",L"喾", L"嚳",L"嗫", L"囁",L"嗳", L"噯",L"嘘", L"噓",L"嘤", L"嚶",L"嘱", L"囑",L"噜", L"嚕",L"嚣", L"囂",L"团", L"團",L"园", L"園",L"囱", L"囪",L"围", L"圍",L"囵", L"圇",L"国", L"國",L"图", L"圖",L"圆", L"圓",L"圣", L"聖",L"圹", L"壙",L"场", L"場",L"坏", L"壞",L"块", L"塊",L"坚", L"堅",L"坛", L"壇",L"坜", L"壢",L"坝", L"壩",L"坞", L"塢",L"坟", L"墳",L"坠", L"墜",L"垄", L"壟",L"垅", L"壠",L"垆", L"壚",L"垒", L"壘",L"垦", L"墾",L"垩", L"堊",L"垫", L"墊",L"垭", L"埡",L"垱", L"壋",L"垲", L"塏",L"垴", L"堖",L"埘", L"塒",L"埙", L"塤",L"埚", L"堝",L"埯", L"垵",L"堑", L"塹",L"堕", L"墮",L"墙", L"牆",L"壮", L"壯",L"声", L"聲",L"壳", L"殼",L"壶", L"壺",L"壸", L"壼",L"处", L"處",L"备", L"備",L"复", L"復",L"够", L"夠",L"头", L"頭",L"夸", L"誇",L"夹", L"夾",L"夺", L"奪",L"奁", L"奩",L"奂", L"奐",L"奋", L"奮",L"奖", L"獎",L"奥", L"奧",L"妆", L"妝",L"妇", L"婦",L"妈", L"媽",L"妩", L"嫵",L"妪", L"嫗",L"妫", L"媯",L"姗", L"姍",L"姹", L"奼",L"娄", L"婁",L"娅", L"婭",L"娆", L"嬈",L"娇", L"嬌",L"娈", L"孌",L"娱", L"娛",L"娲", L"媧",L"娴", L"嫻",L"婳", L"嫿",L"婴", L"嬰",L"婵", L"嬋",L"婶", L"嬸",L"媪", L"媼",L"嫒", L"嬡",L"嫔", L"嬪",L"嫱", L"嬙",L"嬷", L"嬤",L"孙", L"孫",L"学", L"學",L"孪", L"孿",L"宁", L"寧",L"宝", L"寶",L"实", L"實",L"宠", L"寵",L"审", L"審",L"宪", L"憲",L"宫", L"宮",L"宽", L"寬",L"宾", L"賓",L"寝", L"寢",L"对", L"對",L"寻", L"尋",L"导", L"導",L"寿", L"壽",L"将", L"將",L"尔", L"爾",L"尘", L"塵",L"尝", L"嘗",L"尧", L"堯",L"尴", L"尷",L"尸", L"屍",L"尽", L"盡",L"层", L"層",L"屃", L"屓",L"屉", L"屜",L"届", L"屆",L"属", L"屬",L"屡", L"屢",L"屦", L"屨",L"屿", L"嶼",L"岁", L"歲",L"岂", L"豈",L"岖", L"嶇",L"岗", L"崗",L"岘", L"峴",L"岙", L"嶴",L"岚", L"嵐",L"岛", L"島",L"岭", L"嶺",L"岽", L"崬",L"岿", L"巋",L"峄", L"嶧",L"峡", L"峽",L"峣", L"嶢",L"峤", L"嶠",L"峥", L"崢",L"峦", L"巒",L"崂", L"嶗",L"崃", L"崍",L"崄", L"嶮",L"崭", L"嶄",L"嵘", L"嶸",L"嵚", L"嶔",L"嵝", L"嶁",L"巅", L"巔",L"巩", L"鞏",L"巯", L"巰",L"币", L"幣",L"帅", L"帥",L"师", L"師",L"帏", L"幃",L"帐", L"帳",L"帘", L"簾",L"帜", L"幟",L"带", L"帶",L"帧", L"幀",L"帮", L"幫",L"帱", L"幬",L"帻", L"幘",L"帼", L"幗",L"幂", L"冪",L"幞", L"襆",L"并", L"並",L"广", L"廣",L"庆", L"慶",L"庐", L"廬",L"庑", L"廡",L"库", L"庫",L"应", L"應",L"庙", L"廟",L"庞", L"龐",L"废", L"廢",L"廪", L"廩",L"开", L"開",L"异", L"異",L"弃", L"棄",L"弑", L"弒",L"张", L"張",L"弥", L"彌",L"弪", L"弳",L"弯", L"彎",L"弹", L"彈",L"强", L"強",L"归", L"歸",L"当", L"當",L"录", L"錄",L"彦", L"彥",L"彻", L"徹",L"径", L"徑",L"徕", L"徠",L"忆", L"憶",L"忏", L"懺",L"忧", L"憂",L"忾", L"愾",L"怀", L"懷",L"态", L"態",L"怂", L"慫",L"怃", L"憮",L"怄", L"慪",L"怅", L"悵",L"怆", L"愴",L"怜", L"憐",L"总", L"總",L"怼", L"懟",L"怿", L"懌",L"恋", L"戀",L"恒", L"恆",L"恳", L"懇",L"恶", L"惡",L"恸", L"慟",L"恹", L"懨",L"恺", L"愷",L"恻", L"惻",L"恼", L"惱",L"恽", L"惲",L"悦", L"悅",L"悫", L"愨",L"悬", L"懸",L"悭", L"慳",L"悮", L"悞",L"悯", L"憫",L"惊", L"驚",L"惧", L"懼",L"惨", L"慘",L"惩", L"懲",L"惫", L"憊",L"惬", L"愜",L"惭", L"慚",L"惮", L"憚",L"惯", L"慣",L"愠", L"慍",L"愤", L"憤",L"愦", L"憒",L"愿", L"願",L"慑", L"懾",L"懑", L"懣",L"懒", L"懶",L"懔", L"懍",L"戆", L"戇",L"戋", L"戔",L"戏", L"戲",L"戗", L"戧",L"战", L"戰",L"戬", L"戩",L"戯", L"戱",L"户", L"戶",L"扑", L"撲",L"执", L"執",L"扩", L"擴",L"扪", L"捫",L"扫", L"掃",L"扬", L"揚",L"扰", L"擾",L"抚", L"撫",L"抛", L"拋",L"抟", L"摶",L"抠", L"摳",L"抡", L"掄",L"抢", L"搶",L"护", L"護",L"报", L"報",L"担", L"擔",L"拟", L"擬",L"拢", L"攏",L"拣", L"揀",L"拥", L"擁",L"拦", L"攔",L"拧", L"擰",L"拨", L"撥",L"择", L"擇",L"挂", L"掛",L"挚", L"摯",L"挛", L"攣",L"挜", L"掗",L"挝", L"撾",L"挞", L"撻",L"挟", L"挾",L"挠", L"撓",L"挡", L"擋",L"挢", L"撟",L"挣", L"掙",L"挤", L"擠",L"挥", L"揮",L"挦", L"撏",L"捝", L"挩",L"捞", L"撈",L"损", L"損",L"捡", L"撿",L"换", L"換",L"捣", L"搗",L"据", L"據",L"掳", L"擄",L"掴", L"摑",L"掷", L"擲",L"掸", L"撣",L"掺", L"摻",L"掼", L"摜",L"揽", L"攬",L"揾", L"搵",L"揿", L"撳",L"搀", L"攙",L"搁", L"擱",L"搂", L"摟",L"搅", L"攪",L"携", L"攜",L"摄", L"攝",L"摅", L"攄",L"摆", L"擺",L"摇", L"搖",L"摈", L"擯",L"摊", L"攤",L"撄", L"攖",L"撑", L"撐",L"撵", L"攆",L"撷", L"擷",L"撸", L"擼",L"撺", L"攛",L"擞", L"擻",L"攒", L"攢",L"敌", L"敵",L"敛", L"斂",L"数", L"數",L"斋", L"齋",L"斓", L"斕",L"斩", L"斬",L"断", L"斷",L"无", L"無",L"旧", L"舊",L"时", L"時",L"旷", L"曠",L"旸", L"暘",L"昙", L"曇",L"昼", L"晝",L"昽", L"曨",L"显", L"顯",L"晋", L"晉",L"晒", L"曬",L"晓", L"曉",L"晔", L"曄",L"晕", L"暈",L"晖", L"暉",L"暂", L"暫",L"暧", L"曖",L"术", L"術",L"机", L"機",L"杀", L"殺",L"杂", L"雜",L"权", L"權",L"杆", L"桿",L"条", L"條",L"来", L"來",L"杨", L"楊",L"杩", L"榪",L"杰", L"傑",L"极", L"極",L"构", L"構",L"枞", L"樅",L"枢", L"樞",L"枣", L"棗",L"枥", L"櫪",L"枧", L"梘",L"枨", L"棖",L"枪", L"槍",L"枫", L"楓",L"枭", L"梟",L"柜", L"櫃",L"柠", L"檸",L"柽", L"檉",L"栀", L"梔",L"栅", L"柵",L"标", L"標",L"栈", L"棧",L"栉", L"櫛",L"栊", L"櫳",L"栋", L"棟",L"栌", L"櫨",L"栎", L"櫟",L"栏", L"欄",L"树", L"樹",L"栖", L"棲",L"样", L"樣",L"栾", L"欒",L"桠", L"椏",L"桡", L"橈",L"桢", L"楨",L"档", L"檔",L"桤", L"榿",L"桥", L"橋",L"桦", L"樺",L"桧", L"檜",L"桨", L"槳",L"桩", L"樁",L"梦", L"夢",L"梼", L"檮",L"梾", L"棶",L"梿", L"槤",L"检", L"檢",L"棁", L"梲",L"棂", L"欞",L"椁", L"槨",L"椟", L"櫝",L"椠", L"槧",L"椤", L"欏",L"椭", L"橢",L"楼", L"樓",L"榄", L"欖",L"榅", L"榲",L"榇", L"櫬",L"榈", L"櫚",L"榉", L"櫸",L"槚", L"檟",L"槛", L"檻",L"槟", L"檳",L"槠", L"櫧",L"横", L"橫",L"樯", L"檣",L"樱", L"櫻",L"橥", L"櫫",L"橱", L"櫥",L"橹", L"櫓",L"橼", L"櫞",L"檩", L"檁",L"欢", L"歡",L"欤", L"歟",L"欧", L"歐",L"歼", L"殲",L"殁", L"歿",L"殇", L"殤",L"残", L"殘",L"殒", L"殞",L"殓", L"殮",L"殚", L"殫",L"殡", L"殯",L"殴", L"毆",L"毁", L"毀",L"毂", L"轂",L"毕", L"畢",L"毙", L"斃",L"毡", L"氈",L"毵", L"毿",L"氇", L"氌",L"气", L"氣",L"氢", L"氫",L"氩", L"氬",L"氲", L"氳",L"汇", L"匯",L"汉", L"漢",L"汤", L"湯",L"汹", L"洶",L"沟", L"溝",L"没", L"沒",L"沣", L"灃",L"沤", L"漚",L"沥", L"瀝",L"沦", L"淪",L"沧", L"滄",L"沩", L"溈",L"沪", L"滬",L"泞", L"濘",L"泪", L"淚",L"泶", L"澩",L"泷", L"瀧",L"泸", L"瀘",L"泺", L"濼",L"泻", L"瀉",L"泼", L"潑",L"泽", L"澤",L"泾", L"涇",L"洁", L"潔",L"洒", L"灑",L"洼", L"窪",L"浃", L"浹",L"浅", L"淺",L"浆", L"漿",L"浇", L"澆",L"浈", L"湞",L"浊", L"濁",L"测", L"測",L"浍", L"澮",L"济", L"濟",L"浏", L"瀏",L"浐", L"滻",L"浑", L"渾",L"浒", L"滸",L"浓", L"濃",L"浔", L"潯",L"涂", L"塗",L"涛", L"濤",L"涝", L"澇",L"涞", L"淶",L"涟", L"漣",L"涠", L"潿",L"涡", L"渦",L"涣", L"渙",L"涤", L"滌",L"润", L"潤",L"涧", L"澗",L"涨", L"漲",L"涩", L"澀",L"渊", L"淵",L"渌", L"淥",L"渍", L"漬",L"渎", L"瀆",L"渐", L"漸",L"渑", L"澠",L"渔", L"漁",L"渖", L"瀋",L"渗", L"滲",L"温", L"溫",L"湾", L"灣",L"湿", L"濕",L"溃", L"潰",L"溅", L"濺",L"溆", L"漵",L"滗", L"潷",L"滚", L"滾",L"滞", L"滯",L"滟", L"灧",L"滠", L"灄",L"满", L"滿",L"滢", L"瀅",L"滤", L"濾",L"滥", L"濫",L"滦", L"灤",L"滨", L"濱",L"滩", L"灘",L"滪", L"澦",L"漤", L"灠",L"潆", L"瀠",L"潇", L"瀟",L"潋", L"瀲",L"潍", L"濰",L"潜", L"潛",L"潴", L"瀦",L"澜", L"瀾",L"濑", L"瀨",L"濒", L"瀕",L"灏", L"灝",L"灭", L"滅",L"灯", L"燈",L"灵", L"靈",L"灾", L"災",L"灿", L"燦",L"炀", L"煬",L"炉", L"爐",L"炖", L"燉",L"炜", L"煒",L"炝", L"熗",L"点", L"點",L"炼", L"煉",L"炽", L"熾",L"烁", L"爍",L"烂", L"爛",L"烃", L"烴",L"烛", L"燭",L"烟", L"煙",L"烦", L"煩",L"烧", L"燒",L"烨", L"燁",L"烩", L"燴",L"烫", L"燙",L"烬", L"燼",L"热", L"熱",L"焕", L"煥",L"焖", L"燜",L"焘", L"燾",L"煴", L"熅",L"爱", L"愛",L"爷", L"爺",L"牍", L"牘",L"牦", L"氂",L"牵", L"牽",L"牺", L"犧",L"犊", L"犢",L"状", L"狀",L"犷", L"獷",L"犸", L"獁",L"犹", L"猶",L"狈", L"狽",L"狝", L"獮",L"狞", L"獰",L"独", L"獨",L"狭", L"狹",L"狮", L"獅",L"狯", L"獪",L"狰", L"猙",L"狱", L"獄",L"狲", L"猻",L"猃", L"獫",L"猎", L"獵",L"猕", L"獼",L"猡", L"玀",L"猪", L"豬",L"猫", L"貓",L"猬", L"蝟",L"献", L"獻",L"獭", L"獺",L"玑", L"璣",L"玚", L"瑒",L"玛", L"瑪",L"玮", L"瑋",L"环", L"環",L"现", L"現",L"玱", L"瑲",L"玺", L"璽",L"珐", L"琺",L"珑", L"瓏",L"珰", L"璫",L"珲", L"琿",L"琏", L"璉",L"琐", L"瑣",L"琼", L"瓊",L"瑶", L"瑤",L"瑷", L"璦",L"璎", L"瓔",L"瓒", L"瓚",L"瓯", L"甌",L"电", L"電",L"画", L"畫",L"畅", L"暢",L"畴", L"疇",L"疖", L"癤",L"疗", L"療",L"疟", L"瘧",L"疠", L"癘",L"疡", L"瘍",L"疬", L"癧",L"疭", L"瘲",L"疮", L"瘡",L"疯", L"瘋",L"疱", L"皰",L"疴", L"痾",L"痈", L"癰",L"痉", L"痙",L"痒", L"癢",L"痖", L"瘂",L"痨", L"癆",L"痪", L"瘓",L"痫", L"癇",L"瘅", L"癉",L"瘆", L"瘮",L"瘗", L"瘞",L"瘘", L"瘺",L"瘪", L"癟",L"瘫", L"癱",L"瘾", L"癮",L"瘿", L"癭",L"癞", L"癩",L"癣", L"癬",L"癫", L"癲",L"皑", L"皚",L"皱", L"皺",L"皲", L"皸",L"盏", L"盞",L"盐", L"鹽",L"监", L"監",L"盖", L"蓋",L"盗", L"盜",L"盘", L"盤",L"眍", L"瞘",L"眦", L"眥",L"眬", L"矓",L"睁", L"睜",L"睐", L"睞",L"睑", L"瞼",L"瞆", L"瞶",L"瞒", L"瞞",L"瞩", L"矚",L"矫", L"矯",L"矶", L"磯",L"矾", L"礬",L"矿", L"礦",L"砀", L"碭",L"码", L"碼",L"砖", L"磚",L"砗", L"硨",L"砚", L"硯",L"砜", L"碸",L"砺", L"礪",L"砻", L"礱",L"砾", L"礫",L"础", L"礎",L"硁", L"硜",L"硕", L"碩",L"硖", L"硤",L"硗", L"磽",L"硙", L"磑",L"确", L"確",L"硷", L"礆",L"碍", L"礙",L"碛", L"磧",L"碜", L"磣",L"碱", L"鹼",L"礼", L"禮",L"祃", L"禡",L"祎", L"禕",L"祢", L"禰",L"祯", L"禎",L"祷", L"禱",L"祸", L"禍",L"禀", L"稟",L"禄", L"祿",L"禅", L"禪",L"离", L"離",L"秃", L"禿",L"秆", L"稈",L"种", L"種",L"积", L"積",L"称", L"稱",L"秽", L"穢",L"秾", L"穠",L"稆", L"穭",L"税", L"稅",L"稣", L"穌",L"稳", L"穩",L"穑", L"穡",L"穷", L"窮",L"窃", L"竊",L"窍", L"竅",L"窎", L"窵",L"窑", L"窯",L"窜", L"竄",L"窝", L"窩",L"窥", L"窺",L"窦", L"竇",L"窭", L"窶",L"竖", L"豎",L"竞", L"競",L"笃", L"篤",L"笋", L"筍",L"笔", L"筆",L"笕", L"筧",L"笺", L"箋",L"笼", L"籠",L"笾", L"籩",L"筑", L"築",L"筚", L"篳",L"筛", L"篩",L"筜", L"簹",L"筝", L"箏",L"筹", L"籌",L"筼", L"篔",L"签", L"簽",L"简", L"簡",L"箓", L"籙",L"箦", L"簀",L"箧", L"篋",L"箨", L"籜",L"箩", L"籮",L"箪", L"簞",L"箫", L"簫",L"篑", L"簣",L"篓", L"簍",L"篮", L"籃",L"篱", L"籬",L"簖", L"籪",L"籁", L"籟",L"籴", L"糴",L"类", L"類",L"籼", L"秈",L"粜", L"糶",L"粝", L"糲",L"粤", L"粵",L"粪", L"糞",L"粮", L"糧",L"糁", L"糝",L"糇", L"餱",L"紧", L"緊",L"絷", L"縶",L"纟", L"糹",L"纠", L"糾",L"纡", L"紆",L"红", L"紅",L"纣", L"紂",L"纤", L"纖",L"纥", L"紇",L"约", L"約",L"级", L"級",L"纨", L"紈",L"纩", L"纊",L"纪", L"紀",L"纫", L"紉",L"纬", L"緯",L"纭", L"紜",L"纮", L"紘",L"纯", L"純",L"纰", L"紕",L"纱", L"紗",L"纲", L"綱",L"纳", L"納",L"纴", L"紝",L"纵", L"縱",L"纶", L"綸",L"纷", L"紛",L"纸", L"紙",L"纹", L"紋",L"纺", L"紡",L"纻", L"紵",L"纼", L"紖",L"纽", L"紐",L"纾", L"紓",L"线", L"線",L"绀", L"紺",L"绁", L"紲",L"绂", L"紱",L"练", L"練",L"组", L"組",L"绅", L"紳",L"细", L"細",L"织", L"織",L"终", L"終",L"绉", L"縐",L"绊", L"絆",L"绋", L"紼",L"绌", L"絀",L"绍", L"紹",L"绎", L"繹",L"经", L"經",L"绐", L"紿",L"绑", L"綁",L"绒", L"絨",L"结", L"結",L"绔", L"絝",L"绕", L"繞",L"绖", L"絰",L"绗", L"絎",L"绘", L"繪",L"给", L"給",L"绚", L"絢",L"绛", L"絳",L"络", L"絡",L"绝", L"絕",L"绞", L"絞",L"统", L"統",L"绠", L"綆",L"绡", L"綃",L"绢", L"絹",L"绣", L"綉",L"绤", L"綌",L"绥", L"綏",L"绦", L"絛",L"继", L"繼",L"绨", L"綈",L"绩", L"績",L"绪", L"緒",L"绫", L"綾",L"绬", L"緓",L"续", L"續",L"绮", L"綺",L"绯", L"緋",L"绰", L"綽",L"绱", L"緔",L"绲", L"緄",L"绳", L"繩",L"维", L"維",L"绵", L"綿",L"绶", L"綬",L"绷", L"綳",L"绸", L"綢",L"绹", L"綯",L"绺", L"綹",L"绻", L"綣",L"综", L"綜",L"绽", L"綻",L"绾", L"綰",L"绿", L"綠",L"缀", L"綴",L"缁", L"緇",L"缂", L"緙",L"缃", L"緗",L"缄", L"緘",L"缅", L"緬",L"缆", L"纜",L"缇", L"緹",L"缈", L"緲",L"缉", L"緝",L"缊", L"縕",L"缋", L"繢",L"缌", L"緦",L"缍", L"綞",L"缎", L"緞",L"缏", L"緶",L"缑", L"緱",L"缒", L"縋",L"缓", L"緩",L"缔", L"締",L"缕", L"縷",L"编", L"編",L"缗", L"緡",L"缘", L"緣",L"缙", L"縉",L"缚", L"縛",L"缛", L"縟",L"缜", L"縝",L"缝", L"縫",L"缞", L"縗",L"缟", L"縞",L"缠", L"纏",L"缡", L"縭",L"缢", L"縊",L"缣", L"縑",L"缤", L"繽",L"缥", L"縹",L"缦", L"縵",L"缧", L"縲",L"缨", L"纓",L"缩", L"縮",L"缪", L"繆",L"缫", L"繅",L"缬", L"纈",L"缭", L"繚",L"缮", L"繕",L"缯", L"繒",L"缰", L"韁",L"缱", L"繾",L"缲", L"繰",L"缳", L"繯",L"缴", L"繳",L"缵", L"纘",L"罂", L"罌",L"网", L"網",L"罗", L"羅",L"罚", L"罰",L"罢", L"罷",L"罴", L"羆",L"羁", L"羈",L"羟", L"羥",L"羡", L"羨",L"翘", L"翹",L"耢", L"耮",L"耧", L"耬",L"耸", L"聳",L"耻", L"恥",L"聂", L"聶",L"聋", L"聾",L"职", L"職",L"聍", L"聹",L"联", L"聯",L"聩", L"聵",L"聪", L"聰",L"肃", L"肅",L"肠", L"腸",L"肤", L"膚",L"肮", L"骯",L"肴", L"餚",L"肾", L"腎",L"肿", L"腫",L"胀", L"脹",L"胁", L"脅",L"胆", L"膽",L"胜", L"勝",L"胧", L"朧",L"胨", L"腖",L"胪", L"臚",L"胫", L"脛",L"胶", L"膠",L"脉", L"脈",L"脍", L"膾",L"脏", L"臟",L"脐", L"臍",L"脑", L"腦",L"脓", L"膿",L"脔", L"臠",L"脚", L"腳",L"脱", L"脫",L"脶", L"腡",L"脸", L"臉",L"腊", L"臘",L"腭", L"齶",L"腻", L"膩",L"腼", L"靦",L"腽", L"膃",L"腾", L"騰",L"膑", L"臏",L"臜", L"臢",L"舆", L"輿",L"舣", L"艤",L"舰", L"艦",L"舱", L"艙",L"舻", L"艫",L"艰", L"艱",L"艳", L"艷",L"艺", L"藝",L"节", L"節",L"芈", L"羋",L"芗", L"薌",L"芜", L"蕪",L"芦", L"蘆",L"苁", L"蓯",L"苇", L"葦",L"苈", L"藶",L"苋", L"莧",L"苌", L"萇",L"苍", L"蒼",L"苎", L"苧",L"苏", L"蘇",L"苧", L"薴",L"苹", L"蘋",L"茎", L"莖",L"茏", L"蘢",L"茑", L"蔦",L"茔", L"塋",L"茕", L"煢",L"茧", L"繭",L"荆", L"荊",L"荐", L"薦",L"荙", L"薘",L"荚", L"莢",L"荛", L"蕘",L"荜", L"蓽",L"荞", L"蕎",L"荟", L"薈",L"荠", L"薺",L"荡", L"盪",L"荣", L"榮",L"荤", L"葷",L"荥", L"滎",L"荦", L"犖",L"荧", L"熒",L"荨", L"蕁",L"荩", L"藎",L"荪", L"蓀",L"荫", L"蔭",L"荬", L"蕒",L"荭", L"葒",L"荮", L"葤",L"药", L"葯",L"莅", L"蒞",L"莱", L"萊",L"莲", L"蓮",L"莳", L"蒔",L"莴", L"萵",L"莶", L"薟",L"获", L"獲",L"莸", L"蕕",L"莹", L"瑩",L"莺", L"鶯",L"莼", L"蒓",L"萝", L"蘿",L"萤", L"螢",L"营", L"營",L"萦", L"縈",L"萧", L"蕭",L"萨", L"薩",L"葱", L"蔥",L"蒇", L"蕆",L"蒉", L"蕢",L"蒋", L"蔣",L"蒌", L"蔞",L"蓝", L"藍",L"蓟", L"薊",L"蓠", L"蘺",L"蓣", L"蕷",L"蓥", L"鎣",L"蓦", L"驀",L"蔂", L"虆",L"蔷", L"薔",L"蔹", L"蘞",L"蔺", L"藺",L"蔼", L"藹",L"蕰", L"薀",L"蕲", L"蘄",L"蕴", L"蘊",L"薮", L"藪",L"藓", L"蘚",L"蘖", L"櫱",L"虏", L"虜",L"虑", L"慮",L"虚", L"虛",L"虫", L"蟲",L"虬", L"虯",L"虮", L"蟣",L"虽", L"雖",L"虾", L"蝦",L"虿", L"蠆",L"蚀", L"蝕",L"蚁", L"蟻",L"蚂", L"螞",L"蚕", L"蠶",L"蚬", L"蜆",L"蛊", L"蠱",L"蛎", L"蠣",L"蛏", L"蟶",L"蛮", L"蠻",L"蛰", L"蟄",L"蛱", L"蛺",L"蛲", L"蟯",L"蛳", L"螄",L"蛴", L"蠐",L"蜕", L"蛻",L"蜗", L"蝸",L"蜡", L"蠟",L"蝇", L"蠅",L"蝈", L"蟈",L"蝉", L"蟬",L"蝎", L"蠍",L"蝼", L"螻",L"蝾", L"蠑",L"螀", L"螿",L"螨", L"蟎",L"蟏", L"蠨",L"衅", L"釁",L"衔", L"銜",L"补", L"補",L"衬", L"襯",L"衮", L"袞",L"袄", L"襖",L"袅", L"裊",L"袆", L"褘",L"袜", L"襪",L"袭", L"襲",L"袯", L"襏",L"装", L"裝",L"裆", L"襠",L"裈", L"褌",L"裢", L"褳",L"裣", L"襝",L"裤", L"褲",L"裥", L"襇",L"褛", L"褸",L"褴", L"襤",L"见", L"見",L"观", L"觀",L"觃", L"覎",L"规", L"規",L"觅", L"覓",L"视", L"視",L"觇", L"覘",L"览", L"覽",L"觉", L"覺",L"觊", L"覬",L"觋", L"覡",L"觌", L"覿",L"觍", L"覥",L"觎", L"覦",L"觏", L"覯",L"觐", L"覲",L"觑", L"覷",L"觞", L"觴",L"触", L"觸",L"觯", L"觶",L"訚", L"誾",L"誉", L"譽",L"誊", L"謄",L"讠", L"訁",L"计", L"計",L"订", L"訂",L"讣", L"訃",L"认", L"認",L"讥", L"譏",L"讦", L"訐",L"讧", L"訌",L"讨", L"討",L"让", L"讓",L"讪", L"訕",L"讫", L"訖",L"讬", L"託",L"训", L"訓",L"议", L"議",L"讯", L"訊",L"记", L"記",L"讱", L"訒",L"讲", L"講",L"讳", L"諱",L"讴", L"謳",L"讵", L"詎",L"讶", L"訝",L"讷", L"訥",L"许", L"許",L"讹", L"訛",L"论", L"論",L"讻", L"訩",L"讼", L"訟",L"讽", L"諷",L"设", L"設",L"访", L"訪",L"诀", L"訣",L"证", L"證",L"诂", L"詁",L"诃", L"訶",L"评", L"評",L"诅", L"詛",L"识", L"識",L"诇", L"詗",L"诈", L"詐",L"诉", L"訴",L"诊", L"診",L"诋", L"詆",L"诌", L"謅",L"词", L"詞",L"诎", L"詘",L"诏", L"詔",L"诐", L"詖",L"译", L"譯",L"诒", L"詒",L"诓", L"誆",L"诔", L"誄",L"试", L"試",L"诖", L"詿",L"诗", L"詩",L"诘", L"詰",L"诙", L"詼",L"诚", L"誠",L"诛", L"誅",L"诜", L"詵",L"话", L"話",L"诞", L"誕",L"诟", L"詬",L"诠", L"詮",L"诡", L"詭",L"询", L"詢",L"诣", L"詣",L"诤", L"諍",L"该", L"該",L"详", L"詳",L"诧", L"詫",L"诨", L"諢",L"诩", L"詡",L"诪", L"譸",L"诫", L"誡",L"诬", L"誣",L"语", L"語",L"诮", L"誚",L"误", L"誤",L"诰", L"誥",L"诱", L"誘",L"诲", L"誨",L"诳", L"誑",L"说", L"說",L"诵", L"誦",L"诶", L"誒",L"请", L"請",L"诸", L"諸",L"诹", L"諏",L"诺", L"諾",L"读", L"讀",L"诼", L"諑",L"诽", L"誹",L"课", L"課",L"诿", L"諉",L"谀", L"諛",L"谁", L"誰",L"谂", L"諗",L"调", L"調",L"谄", L"諂",L"谅", L"諒",L"谆", L"諄",L"谇", L"誶",L"谈", L"談",L"谊", L"誼",L"谋", L"謀",L"谌", L"諶",L"谍", L"諜",L"谎", L"謊",L"谏", L"諫",L"谐", L"諧",L"谑", L"謔",L"谒", L"謁",L"谓", L"謂",L"谔", L"諤",L"谕", L"諭",L"谖", L"諼",L"谗", L"讒",L"谘", L"諮",L"谙", L"諳",L"谚", L"諺",L"谛", L"諦",L"谜", L"謎",L"谝", L"諞",L"谞", L"諝",L"谟", L"謨",L"谠", L"讜",L"谡", L"謖",L"谢", L"謝",L"谣", L"謠",L"谤", L"謗",L"谥", L"謚",L"谦", L"謙",L"谧", L"謐",L"谨", L"謹",L"谩", L"謾",L"谪", L"謫",L"谫", L"譾",L"谬", L"謬",L"谭", L"譚",L"谮", L"譖",L"谯", L"譙",L"谰", L"讕",L"谱", L"譜",L"谲", L"譎",L"谳", L"讞",L"谴", L"譴",L"谵", L"譫",L"谶", L"讖",L"豮", L"豶",L"贝", L"貝",L"贞", L"貞",L"负", L"負",L"贠", L"貟",L"贡", L"貢",L"财", L"財",L"责", L"責",L"贤", L"賢",L"败", L"敗",L"账", L"賬",L"货", L"貨",L"质", L"質",L"贩", L"販",L"贪", L"貪",L"贫", L"貧",L"贬", L"貶",L"购", L"購",L"贮", L"貯",L"贯", L"貫",L"贰", L"貳",L"贱", L"賤",L"贲", L"賁",L"贳", L"貰",L"贴", L"貼",L"贵", L"貴",L"贶", L"貺",L"贷", L"貸",L"贸", L"貿",L"费", L"費",L"贺", L"賀",L"贻", L"貽",L"贼", L"賊",L"贽", L"贄",L"贾", L"賈",L"贿", L"賄",L"赀", L"貲",L"赁", L"賃",L"赂", L"賂",L"赃", L"贓",L"资", L"資",L"赅", L"賅",L"赆", L"贐",L"赇", L"賕",L"赈", L"賑",L"赉", L"賚",L"赊", L"賒",L"赋", L"賦",L"赌", L"賭",L"赍", L"齎",L"赎", L"贖",L"赏", L"賞",L"赐", L"賜",L"赑", L"贔",L"赒", L"賙",L"赓", L"賡",L"赔", L"賠",L"赕", L"賧",L"赖", L"賴",L"赗", L"賵",L"赘", L"贅",L"赙", L"賻",L"赚", L"賺",L"赛", L"賽",L"赜", L"賾",L"赝", L"贗",L"赞", L"贊",L"赟", L"贇",L"赠", L"贈",L"赡", L"贍",L"赢", L"贏",L"赣", L"贛",L"赪", L"赬",L"赵", L"趙",L"赶", L"趕",L"趋", L"趨",L"趱", L"趲",L"趸", L"躉",L"跃", L"躍",L"跄", L"蹌",L"跞", L"躒",L"践", L"踐",L"跶", L"躂",L"跷", L"蹺",L"跸", L"蹕",L"跹", L"躚",L"跻", L"躋",L"踊", L"踴",L"踌", L"躊",L"踪", L"蹤",L"踬", L"躓",L"踯", L"躑",L"蹑", L"躡",L"蹒", L"蹣",L"蹰", L"躕",L"蹿", L"躥",L"躏", L"躪",L"躜", L"躦",L"躯", L"軀",L"軿", L"𫚒",L"车", L"車",L"轧", L"軋",L"轨", L"軌",L"轩", L"軒",L"轪", L"軑",L"轫", L"軔",L"转", L"轉",L"轭", L"軛",L"轮", L"輪",L"软", L"軟",L"轰", L"轟",L"轱", L"軲",L"轲", L"軻",L"轳", L"轤",L"轴", L"軸",L"轵", L"軹",L"轶", L"軼",L"轷", L"軤",L"轸", L"軫",L"轹", L"轢",L"轺", L"軺",L"轻", L"輕",L"轼", L"軾",L"载", L"載",L"轾", L"輊",L"轿", L"轎",L"辀", L"輈",L"辁", L"輇",L"辂", L"輅",L"较", L"較",L"辄", L"輒",L"辅", L"輔",L"辆", L"輛",L"辇", L"輦",L"辈", L"輩",L"辉", L"輝",L"辊", L"輥",L"辋", L"輞",L"辌", L"輬",L"辍", L"輟",L"辎", L"輜",L"辏", L"輳",L"辐", L"輻",L"辑", L"輯",L"辒", L"轀",L"输", L"輸",L"辔", L"轡",L"辕", L"轅",L"辖", L"轄",L"辗", L"輾",L"辘", L"轆",L"辙", L"轍",L"辚", L"轔",L"辞", L"辭",L"辩", L"辯",L"辫", L"辮",L"边", L"邊",L"辽", L"遼",L"达", L"達",L"迁", L"遷",L"过", L"過",L"迈", L"邁",L"运", L"運",L"还", L"還",L"这", L"這",L"进", L"進",L"远", L"遠",L"违", L"違",L"连", L"連",L"迟", L"遲",L"迩", L"邇",L"迳", L"逕",L"迹", L"跡",L"适", L"適",L"选", L"選",L"逊", L"遜",L"递", L"遞",L"逦", L"邐",L"逻", L"邏",L"遗", L"遺",L"遥", L"遙",L"邓", L"鄧",L"邝", L"鄺",L"邬", L"鄔",L"邮", L"郵",L"邹", L"鄒",L"邺", L"鄴",L"邻", L"鄰",L"郏", L"郟",L"郐", L"鄶",L"郑", L"鄭",L"郓", L"鄆",L"郦", L"酈",L"郧", L"鄖",L"郸", L"鄲",L"酂", L"酇",L"酝", L"醞",L"酦", L"醱",L"酱", L"醬",L"酽", L"釅",L"酾", L"釃",L"酿", L"釀",L"释", L"釋",L"鉴", L"鑒",L"銮", L"鑾",L"錾", L"鏨",L"鎭", L"鎮",L"钅", L"釒",L"钆", L"釓",L"钇", L"釔",L"针", L"針",L"钉", L"釘",L"钊", L"釗",L"钋", L"釙",L"钌", L"釕",L"钍", L"釷",L"钎", L"釺",L"钏", L"釧",L"钐", L"釤",L"钑", L"鈒",L"钒", L"釩",L"钓", L"釣",L"钔", L"鍆",L"钕", L"釹",L"钖", L"鍚",L"钗", L"釵",L"钘", L"鈃",L"钙", L"鈣",L"钚", L"鈈",L"钛", L"鈦",L"钜", L"鉅",L"钝", L"鈍",L"钞", L"鈔",L"钟", L"鍾",L"钠", L"鈉",L"钡", L"鋇",L"钢", L"鋼",L"钣", L"鈑",L"钤", L"鈐",L"钥", L"鑰",L"钦", L"欽",L"钧", L"鈞",L"钨", L"鎢",L"钩", L"鉤",L"钪", L"鈧",L"钫", L"鈁",L"钬", L"鈥",L"钭", L"鈄",L"钮", L"鈕",L"钯", L"鈀",L"钰", L"鈺",L"钱", L"錢",L"钲", L"鉦",L"钳", L"鉗",L"钴", L"鈷",L"钵", L"缽",L"钶", L"鈳",L"钷", L"鉕",L"钸", L"鈽",L"钹", L"鈸",L"钺", L"鉞",L"钻", L"鑽",L"钼", L"鉬",L"钽", L"鉭",L"钾", L"鉀",L"钿", L"鈿",L"铀", L"鈾",L"铁", L"鐵",L"铂", L"鉑",L"铃", L"鈴",L"铄", L"鑠",L"铅", L"鉛",L"铆", L"鉚",L"铇", L"鉋",L"铈", L"鈰",L"铉", L"鉉",L"铊", L"鉈",L"铋", L"鉍",L"铌", L"鈮",L"铍", L"鈹",L"铎", L"鐸",L"铏", L"鉶",L"铐", L"銬",L"铑", L"銠",L"铒", L"鉺",L"铓", L"鋩",L"铔", L"錏",L"铕", L"銪",L"铖", L"鋮",L"铗", L"鋏",L"铘", L"鋣",L"铙", L"鐃",L"铚", L"銍",L"铛", L"鐺",L"铜", L"銅",L"铝", L"鋁",L"铞", L"銱",L"铟", L"銦",L"铠", L"鎧",L"铡", L"鍘",L"铢", L"銖",L"铣", L"銑",L"铤", L"鋌",L"铥", L"銩",L"铦", L"銛",L"铧", L"鏵",L"铨", L"銓",L"铩", L"鎩",L"铪", L"鉿",L"铫", L"銚",L"铬", L"鉻",L"铭", L"銘",L"铮", L"錚",L"铯", L"銫",L"铰", L"鉸",L"铱", L"銥",L"铲", L"鏟",L"铳", L"銃",L"铴", L"鐋",L"铵", L"銨",L"银", L"銀",L"铷", L"銣",L"铸", L"鑄",L"铹", L"鐒",L"铺", L"鋪",L"铻", L"鋙",L"铼", L"錸",L"铽", L"鋱",L"链", L"鏈",L"铿", L"鏗",L"销", L"銷",L"锁", L"鎖",L"锂", L"鋰",L"锃", L"鋥",L"锄", L"鋤",L"锅", L"鍋",L"锆", L"鋯",L"锇", L"鋨",L"锈", L"銹",L"锉", L"銼",L"锊", L"鋝",L"锋", L"鋒",L"锌", L"鋅",L"锍", L"鋶",L"锎", L"鐦",L"锏", L"鐧",L"锐", L"銳",L"锑", L"銻",L"锒", L"鋃",L"锓", L"鋟",L"锔", L"鋦",L"锕", L"錒",L"锖", L"錆",L"锗", L"鍺",L"锘", L"鍩",L"错", L"錯",L"锚", L"錨",L"锛", L"錛",L"锜", L"錡",L"锝", L"鍀",L"锞", L"錁",L"锟", L"錕",L"锠", L"錩",L"锡", L"錫",L"锢", L"錮",L"锣", L"鑼",L"锤", L"錘",L"锥", L"錐",L"锦", L"錦",L"锧", L"鑕",L"锨", L"杴",L"锩", L"錈",L"锪", L"鍃",L"锫", L"錇",L"锬", L"錟",L"锭", L"錠",L"键", L"鍵",L"锯", L"鋸",L"锰", L"錳",L"锱", L"錙",L"锲", L"鍥",L"锳", L"鍈",L"锴", L"鍇",L"锵", L"鏘",L"锶", L"鍶",L"锷", L"鍔",L"锸", L"鍤",L"锹", L"鍬",L"锺", L"鍾",L"锻", L"鍛",L"锼", L"鎪",L"锽", L"鍠",L"锾", L"鍰",L"锿", L"鎄",L"镀", L"鍍",L"镁", L"鎂",L"镂", L"鏤",L"镃", L"鎡",L"镄", L"鐨",L"镅", L"鎇",L"镆", L"鏌",L"镇", L"鎮",L"镈", L"鎛",L"镉", L"鎘",L"镊", L"鑷",L"镋", L"鎲",L"镌", L"鐫",L"镍", L"鎳",L"镎", L"鎿",L"镏", L"鎦",L"镐", L"鎬",L"镑", L"鎊",L"镒", L"鎰",L"镓", L"鎵",L"镔", L"鑌",L"镕", L"鎔",L"镖", L"鏢",L"镗", L"鏜",L"镘", L"鏝",L"镙", L"鏍",L"镚", L"鏰",L"镛", L"鏞",L"镜", L"鏡",L"镝", L"鏑",L"镞", L"鏃",L"镟", L"鏇",L"镠", L"鏐",L"镡", L"鐔",L"镢", L"钁",L"镣", L"鐐",L"镤", L"鏷",L"镥", L"鑥",L"镦", L"鐓",L"镧", L"鑭",L"镨", L"鐠",L"镩", L"鑹",L"镪", L"鏹",L"镫", L"鐙",L"镬", L"鑊",L"镭", L"鐳",L"镮", L"鐶",L"镯", L"鐲",L"镰", L"鐮",L"镱", L"鐿",L"镲", L"鑔",L"镳", L"鑣",L"镴", L"鑞",L"镵", L"鑱",L"镶", L"鑲",L"长", L"長",L"门", L"門",L"闩", L"閂",L"闪", L"閃",L"闫", L"閆",L"闬", L"閈",L"闭", L"閉",L"问", L"問",L"闯", L"闖",L"闰", L"閏",L"闱", L"闈",L"闲", L"閑",L"闳", L"閎",L"间", L"間",L"闵", L"閔",L"闶", L"閌",L"闷", L"悶",L"闸", L"閘",L"闹", L"鬧",L"闺", L"閨",L"闻", L"聞",L"闼", L"闥",L"闽", L"閩",L"闾", L"閭",L"闿", L"闓",L"阀", L"閥",L"阁", L"閣",L"阂", L"閡",L"阃", L"閫",L"阄", L"鬮",L"阅", L"閱",L"阆", L"閬",L"阇", L"闍",L"阈", L"閾",L"阉", L"閹",L"阊", L"閶",L"阋", L"鬩",L"阌", L"閿",L"阍", L"閽",L"阎", L"閻",L"阏", L"閼",L"阐", L"闡",L"阑", L"闌",L"阒", L"闃",L"阓", L"闠",L"阔", L"闊",L"阕", L"闋",L"阖", L"闔",L"阗", L"闐",L"阘", L"闒",L"阙", L"闕",L"阚", L"闞",L"阛", L"闤",L"队", L"隊",L"阳", L"陽",L"阴", L"陰",L"阵", L"陣",L"阶", L"階",L"际", L"際",L"陆", L"陸",L"陇", L"隴",L"陈", L"陳",L"陉", L"陘",L"陕", L"陝",L"陧", L"隉",L"陨", L"隕",L"险", L"險",L"随", L"隨",L"隐", L"隱",L"隶", L"隸",L"隽", L"雋",L"难", L"難",L"雏", L"雛",L"雠", L"讎",L"雳", L"靂",L"雾", L"霧",L"霁", L"霽",L"霡", L"霢",L"霭", L"靄",L"靓", L"靚",L"静", L"靜",L"靥", L"靨",L"鞑", L"韃",L"鞒", L"鞽",L"鞯", L"韉",L"鞲", L"韝",L"韦", L"韋",L"韧", L"韌",L"韨", L"韍",L"韩", L"韓",L"韪", L"韙",L"韫", L"韞",L"韬", L"韜",L"韵", L"韻",L"页", L"頁",L"顶", L"頂",L"顷", L"頃",L"顸", L"頇",L"项", L"項",L"顺", L"順",L"须", L"須",L"顼", L"頊",L"顽", L"頑",L"顾", L"顧",L"顿", L"頓",L"颀", L"頎",L"颁", L"頒",L"颂", L"頌",L"颃", L"頏",L"预", L"預",L"颅", L"顱",L"领", L"領",L"颇", L"頗",L"颈", L"頸",L"颉", L"頡",L"颊", L"頰",L"颋", L"頲",L"颌", L"頜",L"颍", L"潁",L"颎", L"熲",L"颏", L"頦",L"颐", L"頤",L"频", L"頻",L"颒", L"頮",L"颓", L"頹",L"颔", L"頷",L"颕", L"頴",L"颖", L"穎",L"颗", L"顆",L"题", L"題",L"颙", L"顒",L"颚", L"顎",L"颛", L"顓",L"颜", L"顏",L"额", L"額",L"颞", L"顳",L"颟", L"顢",L"颠", L"顛",L"颡", L"顙",L"颢", L"顥",L"颤", L"顫",L"颥", L"顬",L"颦", L"顰",L"颧", L"顴",L"风", L"風",L"飏", L"颺",L"飐", L"颭",L"飑", L"颮",L"飒", L"颯",L"飓", L"颶",L"飔", L"颸",L"飕", L"颼",L"飖", L"颻",L"飗", L"飀",L"飘", L"飄",L"飙", L"飆",L"飚", L"飈",L"飞", L"飛",L"飨", L"饗",L"餍", L"饜",L"饣", L"飠",L"饤", L"飣",L"饥", L"飢",L"饦", L"飥",L"饧", L"餳",L"饨", L"飩",L"饩", L"餼",L"饪", L"飪",L"饫", L"飫",L"饬", L"飭",L"饭", L"飯",L"饮", L"飲",L"饯", L"餞",L"饰", L"飾",L"饱", L"飽",L"饲", L"飼",L"饳", L"飿",L"饴", L"飴",L"饵", L"餌",L"饶", L"饒",L"饷", L"餉",L"饸", L"餄",L"饹", L"餎",L"饺", L"餃",L"饻", L"餏",L"饼", L"餅",L"饽", L"餑",L"饾", L"餖",L"饿", L"餓",L"馀", L"餘",L"馁", L"餒",L"馂", L"餕",L"馃", L"餜",L"馄", L"餛",L"馅", L"餡",L"馆", L"館",L"馇", L"餷",L"馈", L"饋",L"馉", L"餶",L"馊", L"餿",L"馋", L"饞",L"馌", L"饁",L"馍", L"饃",L"馎", L"餺",L"馏", L"餾",L"馐", L"饈",L"馑", L"饉",L"馒", L"饅",L"馓", L"饊",L"馔", L"饌",L"馕", L"饢",L"马", L"馬",L"驭", L"馭",L"驮", L"馱",L"驯", L"馴",L"驰", L"馳",L"驱", L"驅",L"驲", L"馹",L"驳", L"駁",L"驴", L"驢",L"驵", L"駔",L"驶", L"駛",L"驷", L"駟",L"驸", L"駙",L"驹", L"駒",L"驺", L"騶",L"驻", L"駐",L"驼", L"駝",L"驽", L"駑",L"驾", L"駕",L"驿", L"驛",L"骀", L"駘",L"骁", L"驍",L"骂", L"罵",L"骃", L"駰",L"骄", L"驕",L"骅", L"驊",L"骆", L"駱",L"骇", L"駭",L"骈", L"駢",L"骉", L"驫",L"骊", L"驪",L"骋", L"騁",L"验", L"驗",L"骍", L"騂",L"骎", L"駸",L"骏", L"駿",L"骐", L"騏",L"骑", L"騎",L"骒", L"騍",L"骓", L"騅",L"骔", L"騌",L"骕", L"驌",L"骖", L"驂",L"骗", L"騙",L"骘", L"騭",L"骙", L"騤",L"骚", L"騷",L"骛", L"騖",L"骜", L"驁",L"骝", L"騮",L"骞", L"騫",L"骟", L"騸",L"骠", L"驃",L"骡", L"騾",L"骢", L"驄",L"骣", L"驏",L"骤", L"驟",L"骥", L"驥",L"骦", L"驦",L"骧", L"驤",L"髅", L"髏",L"髋", L"髖",L"髌", L"髕",L"鬓", L"鬢",L"魇", L"魘",L"魉", L"魎",L"鱼", L"魚",L"鱽", L"魛",L"鱾", L"魢",L"鱿", L"魷",L"鲀", L"魨",L"鲁", L"魯",L"鲂", L"魴",L"鲃", L"䰾",L"鲄", L"魺",L"鲅", L"鮁",L"鲆", L"鮃",L"鲇", L"鯰",L"鲈", L"鱸",L"鲉", L"鮋",L"鲊", L"鮓",L"鲋", L"鮒",L"鲌", L"鮊",L"鲍", L"鮑",L"鲎", L"鱟",L"鲏", L"鮍",L"鲐", L"鮐",L"鲑", L"鮭",L"鲒", L"鮚",L"鲓", L"鮳",L"鲔", L"鮪",L"鲕", L"鮞",L"鲖", L"鮦",L"鲗", L"鰂",L"鲘", L"鮜",L"鲙", L"鱠",L"鲚", L"鱭",L"鲛", L"鮫",L"鲜", L"鮮",L"鲝", L"鮺",L"鲞", L"鯗",L"鲟", L"鱘",L"鲠", L"鯁",L"鲡", L"鱺",L"鲢", L"鰱",L"鲣", L"鰹",L"鲤", L"鯉",L"鲥", L"鰣",L"鲦", L"鰷",L"鲧", L"鯀",L"鲨", L"鯊",L"鲩", L"鯇",L"鲪", L"鮶",L"鲫", L"鯽",L"鲬", L"鯒",L"鲭", L"鯖",L"鲮", L"鯪",L"鲯", L"鯕",L"鲰", L"鯫",L"鲱", L"鯡",L"鲲", L"鯤",L"鲳", L"鯧",L"鲴", L"鯝",L"鲵", L"鯢",L"鲶", L"鯰",L"鲷", L"鯛",L"鲸", L"鯨",L"鲹", L"鰺",L"鲺", L"鯴",L"鲻", L"鯔",L"鲼", L"鱝",L"鲽", L"鰈",L"鲾", L"鰏",L"鲿", L"鱨",L"鳀", L"鯷",L"鳁", L"鰮",L"鳂", L"鰃",L"鳃", L"鰓",L"鳄", L"鱷",L"鳅", L"鰍",L"鳆", L"鰒",L"鳇", L"鰉",L"鳈", L"鰁",L"鳉", L"鱂",L"鳊", L"鯿",L"鳋", L"鰠",L"鳌", L"鰲",L"鳍", L"鰭",L"鳎", L"鰨",L"鳏", L"鰥",L"鳐", L"鰩",L"鳑", L"鰟",L"鳒", L"鰜",L"鳓", L"鰳",L"鳔", L"鰾",L"鳕", L"鱈",L"鳖", L"鱉",L"鳗", L"鰻",L"鳘", L"鰵",L"鳙", L"鱅",L"鳚", L"䲁",L"鳛", L"鰼",L"鳜", L"鱖",L"鳝", L"鱔",L"鳞", L"鱗",L"鳟", L"鱒",L"鳠", L"鱯",L"鳡", L"鱤",L"鳢", L"鱧",L"鳣", L"鱣",L"鸟", L"鳥",L"鸠", L"鳩",L"鸡", L"雞",L"鸢", L"鳶",L"鸣", L"鳴",L"鸤", L"鳲",L"鸥", L"鷗",L"鸦", L"鴉",L"鸧", L"鶬",L"鸨", L"鴇",L"鸩", L"鴆",L"鸪", L"鴣",L"鸫", L"鶇",L"鸬", L"鸕",L"鸭", L"鴨",L"鸮", L"鴞",L"鸯", L"鴦",L"鸰", L"鴒",L"鸱", L"鴟",L"鸲", L"鴝",L"鸳", L"鴛",L"鸴", L"鷽",L"鸵", L"鴕",L"鸶", L"鷥",L"鸷", L"鷙",L"鸸", L"鴯",L"鸹", L"鴰",L"鸺", L"鵂",L"鸻", L"鴴",L"鸼", L"鵃",L"鸽", L"鴿",L"鸾", L"鸞",L"鸿", L"鴻",L"鹀", L"鵐",L"鹁", L"鵓",L"鹂", L"鸝",L"鹃", L"鵑",L"鹄", L"鵠",L"鹅", L"鵝",L"鹆", L"鵒",L"鹇", L"鷳",L"鹈", L"鵜",L"鹉", L"鵡",L"鹊", L"鵲",L"鹋", L"鶓",L"鹌", L"鵪",L"鹍", L"鵾",L"鹎", L"鵯",L"鹏", L"鵬",L"鹐", L"鵮",L"鹑", L"鶉",L"鹒", L"鶊",L"鹓", L"鵷",L"鹔", L"鷫",L"鹕", L"鶘",L"鹖", L"鶡",L"鹗", L"鶚",L"鹘", L"鶻",L"鹙", L"鶖",L"鹚", L"鶿",L"鹛", L"鶥",L"鹜", L"鶩",L"鹝", L"鷊",L"鹞", L"鷂",L"鹟", L"鶲",L"鹠", L"鶹",L"鹡", L"鶺",L"鹢", L"鷁",L"鹣", L"鶼",L"鹤", L"鶴",L"鹥", L"鷖",L"鹦", L"鸚",L"鹧", L"鷓",L"鹨", L"鷚",L"鹩", L"鷯",L"鹪", L"鷦",L"鹫", L"鷲",L"鹬", L"鷸",L"鹭", L"鷺",L"鹮", L"䴉",L"鹯", L"鸇",L"鹰", L"鷹",L"鹱", L"鸌",L"鹲", L"鸏",L"鹳", L"鸛",L"鹴", L"鸘",L"鹾", L"鹺",L"麦", L"麥",L"麸", L"麩",L"黄", L"黃",L"黉", L"黌",L"黡", L"黶",L"黩", L"黷",L"黪", L"黲",L"黾", L"黽",L"鼋", L"黿",L"鼍", L"鼉",L"鼗", L"鞀",L"鼹", L"鼴",L"齄", L"齇",L"齐", L"齊",L"齑", L"齏",L"齿", L"齒",L"龀", L"齔",L"龁", L"齕",L"龂", L"齗",L"龃", L"齟",L"龄", L"齡",L"龅", L"齙",L"龆", L"齠",L"龇", L"齜",L"龈", L"齦",L"龉", L"齬",L"龊", L"齪",L"龋", L"齲",L"龌", L"齷",L"龙", L"龍",L"龚", L"龔",L"龛", L"龕",L"龟", L"龜",L"", L"棡",L"𠮶", L"嗰",L"𡒄", L"壈",L"𦈖", L"䌈",L"𨰾", L"鎷",L"𨰿", L"釳",L"𨱀", L"𨥛",L"𨱁", L"鈠",L"𨱂", L"鈋",L"𨱃", L"鈲",L"𨱄", L"鈯",L"𨱅", L"鉁",L"𨱇", L"銶",L"𨱈", L"鋉",L"𨱉", L"鍄",L"𨱊", L"𨧱",L"𨱋", L"錂",L"𨱌", L"鏆",L"𨱍", L"鎯",L"𨱎", L"鍮",L"𨱏", L"鎝",L"𨱐", L"𨫒",L"𨱒", L"鏉",L"𨱓", L"鐎",L"𨱔", L"鐏",L"𨱕", L"𨮂",L"𨸂", L"閍",L"𨸃", L"閐",L"𩏼", L"䪏",L"𩏽", L"𩏪",L"𩏾", L"𩎢",L"𩏿", L"䪘",L"𩐀", L"䪗",L"𩖕", L"𩓣",L"𩖖", L"顃",L"𩖗", L"䫴",L"𩙥", L"颰",L"𩙦", L"𩗀",L"𩙧", L"𩗡",L"𩙨", L"𩘹",L"𩙩", L"𩘀",L"𩙪", L"颷",L"𩙫", L"颾",L"𩙬", L"𩘺",L"𩙭", L"𩘝",L"𩙮", L"䬘",L"𩙯", L"䬝",L"𩙰", L"𩙈",L"𩠅", L"𩟐",L"𩠆", L"𩜦",L"𩠇", L"䭀",L"𩠈", L"䭃",L"𩠋", L"𩝔",L"𩠌", L"餸",L"𩧦", L"𩡺",L"𩧨", L"駎",L"𩧩", L"𩤊",L"𩧪", L"䮾",L"𩧫", L"駚",L"𩧬", L"𩢡",L"𩧭", L"䭿",L"𩧮", L"𩢾",L"𩧯", L"驋",L"𩧰", L"䮝",L"𩧱", L"𩥉",L"𩧲", L"駧",L"𩧳", L"𩢸",L"𩧴", L"駩",L"𩧵", L"𩢴",L"𩧶", L"𩣏",L"𩧺", L"駶",L"𩧻", L"𩣵",L"𩧼", L"𩣺",L"𩧿", L"䮠",L"𩨀", L"騔",L"𩨁", L"䮞",L"𩨃", L"騝",L"𩨄", L"騪",L"𩨅", L"𩤸",L"𩨆", L"𩤙",L"𩨈", L"騟",L"𩨉", L"𩤲",L"𩨊", L"騚",L"𩨋", L"𩥄",L"𩨌", L"𩥑",L"𩨍", L"𩥇",L"𩨏", L"䮳",L"𩨐", L"𩧆",L"𩽹", L"魥",L"𩽺", L"𩵩",L"𩽻", L"𩵹",L"𩽼", L"鯶",L"𩽽", L"𩶱",L"𩽾", L"鮟",L"𩽿", L"𩶰",L"𩾀", L"鮕",L"𩾁", L"鯄",L"𩾃", L"鮸",L"𩾄", L"𩷰",L"𩾅", L"𩸃",L"𩾆", L"𩸦",L"𩾇", L"鯱",L"𩾈", L"䱙",L"𩾊", L"䱬",L"𩾋", L"䱰",L"𩾌", L"鱇",L"𩾎", L"𩽇",L"𪉂", L"䲰",L"𪉃", L"鳼",L"𪉄", L"𩿪",L"𪉅", L"𪀦",L"𪉆", L"鴲",L"𪉈", L"鴜",L"𪉉", L"𪁈",L"𪉊", L"鷨",L"𪉋", L"𪀾",L"𪉌", L"𪁖",L"𪉍", L"鵚",L"𪉎", L"𪂆",L"𪉏", L"𪃏",L"𪉐", L"𪃍",L"𪉑", L"鷔",L"𪉒", L"𪄕",L"𪉔", L"𪄆",L"𪉕", L"𪇳",L"𪎈", L"䴬",L"𪎉", L"麲",L"𪎊", L"麨",L"𪎋", L"䴴",L"𪎌", L"麳",L"𪚏", L"𪘀",L"𪚐", L"𪘯",L"𪞝", L"凙",L"𪡏", L"嗹",L"𪢮", L"圞",L"𪨊", L"㞞",L"𪨗", L"屩",L"𪻐", L"瑽",L"𪾢", L"睍",L"𫁡", L"鴗",L"𫂈", L"䉬",L"𫄨", L"絺",L"𫄸", L"纁",L"𫌀", L"襀",L"𫌨", L"覼",L"𫍙", L"訑",L"𫍟", L"𧦧",L"𫍢", L"譊",L"𫍰", L"諰",L"𫍲", L"謏",L"𫏋", L"蹻",L"𫐄", L"軏",L"𫐆", L"轣",L"𫐉", L"軨",L"𫐐", L"輗",L"𫐓", L"輮",L"𫓧", L"鈇",L"𫓩", L"鏦",L"𫔎", L"鐍",L"𫗠", L"餦",L"𫗦", L"餔",L"𫗧", L"餗",L"𫗮", L"餭",L"𫗴", L"饘",L"𫘝", L"駃",L"𫘣", L"駻",L"𫘤", L"騃",L"𫘨", L"騠",L"𫚈", L"鱮",L"𫚉", L"魟",L"𫚒", L"鮄",L"𫚔", L"鮰",L"𫚕", L"鰤",L"𫚙", L"鯆",L"𫛛", L"鳷",L"𫛞", L"鴃",L"𫛢", L"鸋",L"𫛶", L"鶒",L"𫛸", L"鶗",L"0多只", L"0多隻",L"0天后", L"0天後",L"0只", L"0隻",L"0余", L"0餘",L"1天后", L"1天後",L"1只", L"1隻",L"1余", L"1餘",L"2天后", L"2天後",L"2只", L"2隻",L"2余", L"2餘",L"3天后", L"3天後",L"3只", L"3隻",L"3余", L"3餘",L"4天后", L"4天後",L"4只", L"4隻",L"4余", L"4餘",L"5天后", L"5天後",L"5只", L"5隻",L"5余", L"5餘",L"6天后", L"6天後",L"6只", L"6隻",L"6余", L"6餘",L"7天后", L"7天後",L"7只", L"7隻",L"7余", L"7餘",L"8天后", L"8天後",L"8只", L"8隻",L"8余", L"8餘",L"9天后", L"9天後",L"9只", L"9隻",L"9余", L"9餘",L"·范", L"·范",L"、克制", L"、剋制",L"。克制", L"。剋制",L"〇只", L"〇隻",L"〇余", L"〇餘",L"一干二净", L"一乾二淨",L"一伙人", L"一伙人",L"一伙头", L"一伙頭",L"一伙食", L"一伙食",L"一并", L"一併",L"一个", L"一個",L"一个准", L"一個準",L"一出刊", L"一出刊",L"一出口", L"一出口",L"一出版", L"一出版",L"一出生", L"一出生",L"一出祁山", L"一出祁山",L"一出逃", L"一出逃",L"一前一后", L"一前一後",L"一划", L"一劃",L"一半只", L"一半只",L"一吊钱", L"一吊錢",L"一地里", L"一地裡",L"一伙", L"一夥",L"一天后", L"一天後",L"一天钟", L"一天鐘",L"一干人", L"一干人",L"一干家中", L"一干家中",L"一干弟兄", L"一干弟兄",L"一干弟子", L"一干弟子",L"一干部下", L"一干部下",L"一吊", L"一弔",L"一别头", L"一彆頭",L"一斗斗", L"一斗斗",L"一树百获", L"一樹百穫",L"一准", L"一準",L"一争两丑", L"一爭兩醜",L"一物克一物", L"一物剋一物",L"一目了然", L"一目了然",L"一扎", L"一紮",L"一冲", L"一衝",L"一锅面", L"一鍋麵",L"一只", L"一隻",L"一面食", L"一面食",L"一余", L"一餘",L"一发千钧", L"一髮千鈞",L"一哄而散", L"一鬨而散",L"丁丁当当", L"丁丁當當",L"丁丑", L"丁丑",L"七个", L"七個",L"七出刊", L"七出刊",L"七出口", L"七出口",L"七出版", L"七出版",L"七出生", L"七出生",L"七出祁山", L"七出祁山",L"七出逃", L"七出逃",L"七划", L"七劃",L"七天后", L"七天後",L"七情六欲", L"七情六慾",L"七扎", L"七紮",L"七只", L"七隻",L"七余", L"七餘",L"万俟", L"万俟",L"万旗", L"万旗",L"三个", L"三個",L"三出刊", L"三出刊",L"三出口", L"三出口",L"三出版", L"三出版",L"三出生", L"三出生",L"三出祁山", L"三出祁山",L"三出逃", L"三出逃",L"三天后", L"三天後",L"三征七辟", L"三徵七辟",L"三准", L"三準",L"三扎", L"三紮",L"三统历", L"三統曆",L"三统历史", L"三統歷史",L"三复", L"三複",L"三只", L"三隻",L"三余", L"三餘",L"上梁山", L"上梁山",L"上梁", L"上樑",L"上签名", L"上簽名",L"上签字", L"上簽字",L"上签写", L"上簽寫",L"上签收", L"上簽收",L"上签", L"上籤",L"上药", L"上藥",L"上课钟", L"上課鐘",L"上面糊", L"上面糊",L"下仑路", L"下崙路",L"下于", L"下於",L"下梁", L"下樑",L"下注解", L"下注解",L"下签", L"下籤",L"下药", L"下藥",L"下课钟", L"下課鐘",L"不干不净", L"不乾不淨",L"不占", L"不佔",L"不克自制", L"不克自制",L"不准他", L"不准他",L"不准你", L"不准你",L"不准她", L"不准她",L"不准它", L"不准它",L"不准我", L"不准我",L"不准没", L"不准沒",L"不准翻印", L"不准翻印",L"不准许", L"不准許",L"不准谁", L"不准誰",L"不克制", L"不剋制",L"不前不后", L"不前不後",L"不加自制", L"不加自制",L"不占凶吉", L"不占凶吉",L"不占卜", L"不占卜",L"不占吉凶", L"不占吉凶",L"不占算", L"不占算",L"不好干涉", L"不好干涉",L"不好干预", L"不好干預",L"不好干預", L"不好干預",L"不嫌母丑", L"不嫌母醜",L"不寒而栗", L"不寒而慄",L"不干事", L"不干事",L"不干他", L"不干他",L"不干休", L"不干休",L"不干你", L"不干你",L"不干她", L"不干她",L"不干它", L"不干它",L"不干我", L"不干我",L"不干擾", L"不干擾",L"不干扰", L"不干擾",L"不干涉", L"不干涉",L"不干牠", L"不干牠",L"不干犯", L"不干犯",L"不干预", L"不干預",L"不干預", L"不干預",L"不干", L"不幹",L"不吊", L"不弔",L"不采", L"不採",L"不斗胆", L"不斗膽",L"不断发", L"不斷發",L"不每只", L"不每只",L"不准", L"不準",L"不准确", L"不準確",L"不谷", L"不穀",L"不药而愈", L"不藥而癒",L"不托", L"不託",L"不负所托", L"不負所托",L"不通吊庆", L"不通弔慶",L"不丑", L"不醜",L"不采声", L"不采聲",L"不锈钢", L"不鏽鋼",L"不食干腊", L"不食乾腊",L"不斗", L"不鬥",L"丑三", L"丑三",L"丑婆子", L"丑婆子",L"丑年", L"丑年",L"丑日", L"丑日",L"丑旦", L"丑旦",L"丑时", L"丑時",L"丑月", L"丑月",L"丑表功", L"丑表功",L"丑角", L"丑角",L"且于", L"且於",L"世田谷", L"世田谷",L"世界杯", L"世界盃",L"世界里", L"世界裡",L"世纪钟", L"世紀鐘",L"世纪钟表", L"世紀鐘錶",L"丢丑", L"丟醜",L"并不准", L"並不准",L"并存着", L"並存著",L"并曰入淀", L"並曰入澱",L"并发动", L"並發動",L"并发展", L"並發展",L"并发现", L"並發現",L"并发表", L"並發表",L"中国国际信托投资公司", L"中國國際信托投資公司",L"中型钟", L"中型鐘",L"中型钟表面", L"中型鐘表面",L"中型钟表", L"中型鐘錶",L"中型钟面", L"中型鐘面",L"中仑", L"中崙",L"中岳", L"中嶽",L"中文里", L"中文裡",L"中于", L"中於",L"中签", L"中籤",L"中美发表", L"中美發表",L"中药", L"中藥",L"丰儀", L"丰儀",L"丰仪", L"丰儀",L"丰南", L"丰南",L"丰台", L"丰台",L"丰姿", L"丰姿",L"丰容", L"丰容",L"丰度", L"丰度",L"丰情", L"丰情",L"丰标", L"丰標",L"丰標不凡", L"丰標不凡",L"丰标不凡", L"丰標不凡",L"丰神", L"丰神",L"丰茸", L"丰茸",L"丰采", L"丰采",L"丰韵", L"丰韻",L"丰韻", L"丰韻",L"丸药", L"丸藥",L"丹药", L"丹藥",L"主仆", L"主僕",L"主干", L"主幹",L"主钟差", L"主鐘差",L"主钟曲线", L"主鐘曲線",L"么么小丑", L"么麼小丑",L"之一只", L"之一只",L"之二只", L"之二只",L"之八九只", L"之八九只",L"之后", L"之後",L"之征", L"之徵",L"之托", L"之託",L"之钟", L"之鐘",L"之余", L"之餘",L"乙丑", L"乙丑",L"九世之仇", L"九世之讎",L"九个", L"九個",L"九出刊", L"九出刊",L"九出口", L"九出口",L"九出版", L"九出版",L"九出生", L"九出生",L"九出祁山", L"九出祁山",L"九出逃", L"九出逃",L"九划", L"九劃",L"九天后", L"九天後",L"九谷", L"九穀",L"九扎", L"九紮",L"九只", L"九隻",L"九余", L"九餘",L"九龙表行", L"九龍表行",L"也克制", L"也剋制",L"也斗了胆", L"也斗了膽",L"干干", L"乾乾",L"干干儿的", L"乾乾兒的",L"干干净净", L"乾乾淨淨",L"干井", L"乾井",L"干个够", L"乾個夠",L"干儿", L"乾兒",L"干冰", L"乾冰",L"干冷", L"乾冷",L"干刻版", L"乾刻版",L"干剥剥", L"乾剝剝",L"干卦", L"乾卦",L"干吊着下巴", L"乾吊著下巴",L"干和", L"乾和",L"干咳", L"乾咳",L"干咽", L"乾咽",L"干哥", L"乾哥",L"干哭", L"乾哭",L"干唱", L"乾唱",L"干啼", L"乾啼",L"干乔", L"乾喬",L"干呕", L"乾嘔",L"干哕", L"乾噦",L"干嚎", L"乾嚎",L"干回付", L"乾回付",L"干圆洁净", L"乾圓潔淨",L"干地", L"乾地",L"干坤", L"乾坤",L"干坞", L"乾塢",L"干女", L"乾女",L"干奴才", L"乾奴才",L"干妹", L"乾妹",L"干姊", L"乾姊",L"干娘", L"乾娘",L"干妈", L"乾媽",L"干子", L"乾子",L"干季", L"乾季",L"干尸", L"乾屍",L"干屎橛", L"乾屎橛",L"干巴", L"乾巴",L"干式", L"乾式",L"干弟", L"乾弟",L"干急", L"乾急",L"干性", L"乾性",L"干打雷", L"乾打雷",L"干折", L"乾折",L"干撂台", L"乾撂台",L"干撇下", L"乾撇下",L"干擦", L"乾擦",L"干支剌", L"乾支剌",L"干支支", L"乾支支",L"干敲梆子不卖油", L"乾敲梆子不賣油",L"干料", L"乾料",L"干旱", L"乾旱",L"干暖", L"乾暖",L"干材", L"乾材",L"干村沙", L"乾村沙",L"干杯", L"乾杯",L"干果", L"乾果",L"干枯", L"乾枯",L"干柴", L"乾柴",L"干柴烈火", L"乾柴烈火",L"干梅", L"乾梅",L"干死", L"乾死",L"干池", L"乾池",L"干没", L"乾沒",L"干洗", L"乾洗",L"干涸", L"乾涸",L"干凉", L"乾涼",L"干净", L"乾淨",L"干渠", L"乾渠",L"干渴", L"乾渴",L"干沟", L"乾溝",L"干漆", L"乾漆",L"干涩", L"乾澀",L"干湿", L"乾濕",L"干熬", L"乾熬",L"干热", L"乾熱",L"干熱", L"乾熱",L"干灯盏", L"乾燈盞",L"干燥", L"乾燥",L"干爸", L"乾爸",L"干爹", L"乾爹",L"干爽", L"乾爽",L"干片", L"乾片",L"干生受", L"乾生受",L"干生子", L"乾生子",L"干产", L"乾產",L"干田", L"乾田",L"干疥", L"乾疥",L"干瘦", L"乾瘦",L"干瘪", L"乾癟",L"干癣", L"乾癬",L"干瘾", L"乾癮",L"干白儿", L"乾白兒",L"干的", L"乾的",L"干眼", L"乾眼",L"干瞪眼", L"乾瞪眼",L"干礼", L"乾禮",L"干稿", L"乾稿",L"干笑", L"乾笑",L"干等", L"乾等",L"干篾片", L"乾篾片",L"干粉", L"乾粉",L"干粮", L"乾糧",L"干结", L"乾結",L"干丝", L"乾絲",L"干纲", L"乾綱",L"干绷", L"乾繃",L"干耗", L"乾耗",L"干肉片", L"乾肉片",L"干股", L"乾股",L"干肥", L"乾肥",L"干脆", L"乾脆",L"干花", L"乾花",L"干刍", L"乾芻",L"干苔", L"乾苔",L"干茨腊", L"乾茨臘",L"干茶钱", L"乾茶錢",L"干草", L"乾草",L"干菜", L"乾菜",L"干落", L"乾落",L"干着", L"乾著",L"干姜", L"乾薑",L"干薪", L"乾薪",L"干虔", L"乾虔",L"干号", L"乾號",L"干血浆", L"乾血漿",L"干衣", L"乾衣",L"干裂", L"乾裂",L"干亲", L"乾親",L"乾象历", L"乾象曆",L"乾象曆", L"乾象曆",L"干贝", L"乾貝",L"干货", L"乾貨",L"干躁", L"乾躁",L"干逼", L"乾逼",L"干酪", L"乾酪",L"干酵母", L"乾酵母",L"干醋", L"乾醋",L"干重", L"乾重",L"干量", L"乾量",L"干阿奶", L"乾阿奶",L"干隆", L"乾隆",L"干雷", L"乾雷",L"干电", L"乾電",L"干霍乱", L"乾霍亂",L"干颡", L"乾顙",L"干台", L"乾颱",L"干饭", L"乾飯",L"干馆", L"乾館",L"干糇", L"乾餱",L"干馏", L"乾餾",L"干鱼", L"乾魚",L"干鲜", L"乾鮮",L"干面", L"乾麵",L"乱发", L"亂髮",L"乱哄", L"亂鬨",L"乱哄不过来", L"亂鬨不過來",L"了克制", L"了剋制",L"事后", L"事後",L"事情干脆", L"事情干脆",L"事有斗巧", L"事有鬥巧",L"事迹", L"事迹",L"事都干脆", L"事都干脆",L"二不棱登", L"二不稜登",L"二个", L"二個",L"二出刊", L"二出刊",L"二出口", L"二出口",L"二出版", L"二出版",L"二出生", L"二出生",L"二出祁山", L"二出祁山",L"二出逃", L"二出逃",L"二划", L"二劃",L"二只得", L"二只得",L"二天后", L"二天後",L"二仑", L"二崙",L"二缶钟惑", L"二缶鐘惑",L"二老板", L"二老板",L"二虎相斗", L"二虎相鬥",L"二里头", L"二里頭",L"二里頭", L"二里頭",L"二只", L"二隻",L"二余", L"二餘",L"于丹", L"于丹",L"于于", L"于于",L"于仁泰", L"于仁泰",L"于佳卉", L"于佳卉",L"于伟国", L"于偉國",L"于偉國", L"于偉國",L"于光远", L"于光遠",L"于光遠", L"于光遠",L"于克-蘭多縣", L"于克-蘭多縣",L"于克-兰多县", L"于克-蘭多縣",L"于克勒", L"于克勒",L"于冕", L"于冕",L"于凌奎", L"于凌奎",L"于勒", L"于勒",L"于化虎", L"于化虎",L"于占元", L"于占元",L"于台煙", L"于台煙",L"于台烟", L"于台煙",L"于右任", L"于右任",L"于吉", L"于吉",L"于品海", L"于品海",L"于国桢", L"于國楨",L"于國楨", L"于國楨",L"于坚", L"于堅",L"于堅", L"于堅",L"于大寶", L"于大寶",L"于大宝", L"于大寶",L"于天仁", L"于天仁",L"于奇库杜克", L"于奇庫杜克",L"于奇庫杜克", L"于奇庫杜克",L"于姓", L"于姓",L"于娜", L"于娜",L"于娟", L"于娟",L"于子千", L"于子千",L"于孔兼", L"于孔兼",L"于學忠", L"于學忠",L"于学忠", L"于學忠",L"于家堡", L"于家堡",L"于寘", L"于寘",L"于小伟", L"于小偉",L"于小偉", L"于小偉",L"于小彤", L"于小彤",L"于山", L"于山",L"于山国", L"于山國",L"于山國", L"于山國",L"于帥", L"于帥",L"于帅", L"于帥",L"于幼軍", L"于幼軍",L"于幼军", L"于幼軍",L"于康震", L"于康震",L"于廣洲", L"于廣洲",L"于广洲", L"于廣洲",L"于式枚", L"于式枚",L"于從濂", L"于從濂",L"于从濂", L"于從濂",L"于德海", L"于德海",L"于志宁", L"于志寧",L"于志寧", L"于志寧",L"于思", L"于思",L"于慎行", L"于慎行",L"于慧", L"于慧",L"于成龙", L"于成龍",L"于成龍", L"于成龍",L"于振", L"于振",L"于振武", L"于振武",L"于敏", L"于敏",L"于敏中", L"于敏中",L"于斌", L"于斌",L"于斯塔德", L"于斯塔德",L"于斯納爾斯貝里", L"于斯納爾斯貝里",L"于斯纳尔斯贝里", L"于斯納爾斯貝里",L"于斯达尔", L"于斯達爾",L"于斯達爾", L"于斯達爾",L"于明涛", L"于明濤",L"于明濤", L"于明濤",L"于是之", L"于是之",L"于晨楠", L"于晨楠",L"于晴", L"于晴",L"于會泳", L"于會泳",L"于会泳", L"于會泳",L"于根伟", L"于根偉",L"于根偉", L"于根偉",L"于格", L"于格",L"于樂", L"于樂",L"于树洁", L"于樹潔",L"于樹潔", L"于樹潔",L"于欣源", L"于欣源",L"于正升", L"于正昇",L"于正昇", L"于正昇",L"于正昌", L"于正昌",L"于归", L"于歸",L"于永波", L"于永波",L"于江震", L"于江震",L"于波", L"于波",L"于洪区", L"于洪區",L"于洪區", L"于洪區",L"于浩威", L"于浩威",L"于海洋", L"于海洋",L"于湘兰", L"于湘蘭",L"于湘蘭", L"于湘蘭",L"于漢超", L"于漢超",L"于汉超", L"于漢超",L"于泽尔", L"于澤爾",L"于澤爾", L"于澤爾",L"于涛", L"于濤",L"于濤", L"于濤",L"于爾岑", L"于爾岑",L"于尔岑", L"于爾岑",L"于尔根", L"于爾根",L"于爾根", L"于爾根",L"于尔里克", L"于爾里克",L"于爾里克", L"于爾里克",L"于特森", L"于特森",L"于玉立", L"于玉立",L"于田", L"于田",L"于禁", L"于禁",L"于秀敏", L"于秀敏",L"于素秋", L"于素秋",L"于美人", L"于美人",L"于若木", L"于若木",L"于蔭霖", L"于蔭霖",L"于荫霖", L"于蔭霖",L"于衡", L"于衡",L"于西翰", L"于西翰",L"于謙", L"于謙",L"于谦", L"于謙",L"于貝爾", L"于貝爾",L"于贝尔", L"于貝爾",L"于赠", L"于贈",L"于贈", L"于贈",L"于越", L"于越",L"于军", L"于軍",L"于軍", L"于軍",L"于道泉", L"于道泉",L"于远伟", L"于遠偉",L"于遠偉", L"于遠偉",L"于都縣", L"于都縣",L"于都县", L"于都縣",L"于里察", L"于里察",L"于阗", L"于闐",L"于雙戈", L"于雙戈",L"于双戈", L"于雙戈",L"于震寰", L"于震寰",L"于震环", L"于震環",L"于震環", L"于震環",L"于靖", L"于靖",L"于非闇", L"于非闇",L"于韋斯屈萊", L"于韋斯屈萊",L"于韦斯屈莱", L"于韋斯屈萊",L"于风政", L"于風政",L"于風政", L"于風政",L"于飞", L"于飛",L"于余曲折", L"于餘曲折",L"于凤桐", L"于鳳桐",L"于鳳桐", L"于鳳桐",L"于鳳至", L"于鳳至",L"于凤至", L"于鳳至",L"于默奥", L"于默奧",L"于默奧", L"于默奧",L"云乎", L"云乎",L"云云", L"云云",L"云何", L"云何",L"云为", L"云為",L"云為", L"云為",L"云然", L"云然",L"云尔", L"云爾",L"云：", L"云：",L"五个", L"五個",L"五出刊", L"五出刊",L"五出口", L"五出口",L"五出版", L"五出版",L"五出生", L"五出生",L"五出祁山", L"五出祁山",L"五出逃", L"五出逃",L"五划", L"五劃",L"五天后", L"五天後",L"五岳", L"五嶽",L"五谷", L"五穀",L"五扎", L"五紮",L"五行生克", L"五行生剋",L"五谷王北街", L"五谷王北街",L"五谷王南街", L"五谷王南街",L"五只", L"五隻",L"五余", L"五餘",L"五出", L"五齣",L"井干摧败", L"井榦摧敗",L"井里", L"井裡",L"亚于", L"亞於",L"亚美尼亚历", L"亞美尼亞曆",L"交托", L"交託",L"交游", L"交遊",L"交哄", L"交鬨",L"亦云", L"亦云",L"亦庄亦谐", L"亦莊亦諧",L"亮丑", L"亮醜",L"亮钟", L"亮鐘",L"人云", L"人云",L"人参加", L"人參加",L"人参展", L"人參展",L"人参战", L"人參戰",L"人参拜", L"人參拜",L"人参政", L"人參政",L"人参照", L"人參照",L"人参看", L"人參看",L"人参禅", L"人參禪",L"人参考", L"人參考",L"人参与", L"人參與",L"人参见", L"人參見",L"人参观", L"人參觀",L"人参谋", L"人參謀",L"人参议", L"人參議",L"人参赞", L"人參贊",L"人参透", L"人參透",L"人参选", L"人參選",L"人参酌", L"人參酌",L"人参阅", L"人參閱",L"人后", L"人後",L"人欲", L"人慾",L"人物志", L"人物誌",L"人参", L"人蔘",L"什锦面", L"什錦麵",L"什么", L"什麼",L"仇仇", L"仇讎",L"今后", L"今後",L"他克制", L"他剋制",L"他钟", L"他鐘",L"付托", L"付託",L"仙后座", L"仙后座",L"仙药", L"仙藥",L"代码表", L"代碼表",L"令人发指", L"令人髮指",L"以后", L"以後",L"以自制", L"以自制",L"仰药", L"仰藥",L"件钟", L"件鐘",L"任何表", L"任何錶",L"任何钟", L"任何鐘",L"任何钟表", L"任何鐘錶",L"任教于", L"任教於",L"任于", L"任於",L"仿制", L"仿製",L"企划", L"企劃",L"伊于湖底", L"伊于湖底",L"伊府面", L"伊府麵",L"伊斯兰教历", L"伊斯蘭教曆",L"伊斯兰教历史", L"伊斯蘭教歷史",L"伊斯兰历", L"伊斯蘭曆",L"伊斯兰历史", L"伊斯蘭歷史",L"伊郁", L"伊鬱",L"伏几", L"伏几",L"伐罪吊民", L"伐罪弔民",L"休征", L"休徵",L"伙头", L"伙頭",L"伴游", L"伴遊",L"似于", L"似於",L"但云", L"但云",L"布于", L"佈於",L"布道", L"佈道",L"布雷、", L"佈雷、",L"布雷。", L"佈雷。",L"布雷封锁", L"佈雷封鎖",L"布雷的", L"佈雷的",L"布雷艇", L"佈雷艇",L"布雷舰", L"佈雷艦",L"布雷速度", L"佈雷速度",L"布雷，", L"佈雷，",L"布雷；", L"佈雷；",L"位于", L"位於",L"位准", L"位準",L"低洼", L"低洼",L"住扎", L"住紮",L"占0", L"佔0",L"占1", L"佔1",L"占2", L"佔2",L"占3", L"佔3",L"占4", L"佔4",L"占5", L"佔5",L"占6", L"佔6",L"占7", L"佔7",L"占8", L"佔8",L"占9", L"佔9",L"占A", L"佔A",L"占B", L"佔B",L"占C", L"佔C",L"占D", L"佔D",L"占E", L"佔E",L"占F", L"佔F",L"占G", L"佔G",L"占H", L"佔H",L"占I", L"佔I",L"占J", L"佔J",L"占K", L"佔K",L"占L", L"佔L",L"占M", L"佔M",L"占N", L"佔N",L"占O", L"佔O",L"占P", L"佔P",L"占Q", L"佔Q",L"占R", L"佔R",L"占S", L"佔S",L"占T", L"佔T",L"占U", L"佔U",L"占V", L"佔V",L"占W", L"佔W",L"占X", L"佔X",L"占Y", L"佔Y",L"占Z", L"佔Z",L"占a", L"佔a",L"占b", L"佔b",L"占c", L"佔c",L"占d", L"佔d",L"占e", L"佔e",L"占f", L"佔f",L"占g", L"佔g",L"占h", L"佔h",L"占i", L"佔i",L"占j", L"佔j",L"占k", L"佔k",L"占l", L"佔l",L"占m", L"佔m",L"占n", L"佔n",L"占o", L"佔o",L"占p", L"佔p",L"占q", L"佔q",L"占r", L"佔r",L"占s", L"佔s",L"占t", L"佔t",L"占u", L"佔u",L"占v", L"佔v",L"占w", L"佔w",L"占x", L"佔x",L"占y", L"佔y",L"占z", L"佔z",L"占〇", L"佔〇",L"占一", L"佔一",L"占七", L"佔七",L"占万", L"佔万",L"占三", L"佔三",L"占上风", L"佔上風",L"占下", L"佔下",L"占下风", L"佔下風",L"占不占", L"佔不佔",L"占不足", L"佔不足",L"占世界", L"佔世界",L"占中", L"佔中",L"占主", L"佔主",L"占九", L"佔九",L"占了", L"佔了",L"占二", L"佔二",L"占五", L"佔五",L"占人便宜", L"佔人便宜",L"占位", L"佔位",L"占住", L"佔住",L"占占", L"佔佔",L"占便宜", L"佔便宜",L"占俄", L"佔俄",L"占个", L"佔個",L"占个位", L"佔個位",L"占停车", L"佔停車",L"占亿", L"佔億",L"占优", L"佔優",L"占先", L"佔先",L"占光", L"佔光",L"占全", L"佔全",L"占两", L"佔兩",L"占八", L"佔八",L"占六", L"佔六",L"占分", L"佔分",L"占到", L"佔到",L"占加", L"佔加",L"占劣", L"佔劣",L"占北", L"佔北",L"占十", L"佔十",L"占千", L"佔千",L"占半", L"佔半",L"占南", L"佔南",L"占印", L"佔印",L"占去", L"佔去",L"占取", L"佔取",L"占台", L"佔台",L"占哺乳", L"佔哺乳",L"占嗫", L"佔囁",L"占四", L"佔四",L"占国内", L"佔國內",L"占在", L"佔在",L"占地", L"佔地",L"占场", L"佔場",L"占压", L"佔壓",L"占多", L"佔多",L"占大", L"佔大",L"占好", L"佔好",L"占小", L"佔小",L"占少", L"佔少",L"占局部", L"佔局部",L"占屋", L"佔屋",L"占山", L"佔山",L"占市场", L"佔市場",L"占平均", L"佔平均",L"占床", L"佔床",L"占座", L"佔座",L"占后", L"佔後",L"占得", L"佔得",L"占德", L"佔德",L"占掉", L"佔掉",L"占据", L"佔據",L"占整体", L"佔整體",L"占新", L"佔新",L"占有", L"佔有",L"占有欲", L"佔有慾",L"占东", L"佔東",L"占查", L"佔查",L"占次", L"佔次",L"占比", L"佔比",L"占法", L"佔法",L"占满", L"佔滿",L"占澳", L"佔澳",L"占为", L"佔為",L"占率", L"佔率",L"占用", L"佔用",L"占毕", L"佔畢",L"占百", L"佔百",L"占尽", L"佔盡",L"占稳", L"佔穩",L"占网", L"佔網",L"占线", L"佔線",L"占总", L"佔總",L"占缺", L"佔缺",L"占美", L"佔美",L"占耕", L"佔耕",L"占至多", L"佔至多",L"占至少", L"佔至少",L"占英", L"佔英",L"占着", L"佔著",L"占葡", L"佔葡",L"占苏", L"佔蘇",L"占西", L"佔西",L"占资源", L"佔資源",L"占起", L"佔起",L"占超过", L"佔超過",L"占过", L"佔過",L"占道", L"佔道",L"占零", L"佔零",L"占領", L"佔領",L"占领", L"佔領",L"占头", L"佔頭",L"占头筹", L"佔頭籌",L"占饭", L"佔飯",L"占香", L"佔香",L"占马", L"佔馬",L"占高枝儿", L"佔高枝兒",L"占０", L"佔０",L"占１", L"佔１",L"占２", L"佔２",L"占３", L"佔３",L"占４", L"佔４",L"占５", L"佔５",L"占６", L"佔６",L"占７", L"佔７",L"占８", L"佔８",L"占９", L"佔９",L"占Ａ", L"佔Ａ",L"占Ｂ", L"佔Ｂ",L"占Ｃ", L"佔Ｃ",L"占Ｄ", L"佔Ｄ",L"占Ｅ", L"佔Ｅ",L"占Ｆ", L"佔Ｆ",L"占Ｇ", L"佔Ｇ",L"占Ｈ", L"佔Ｈ",L"占Ｉ", L"佔Ｉ",L"占Ｊ", L"佔Ｊ",L"占Ｋ", L"佔Ｋ",L"占Ｌ", L"佔Ｌ",L"占Ｍ", L"佔Ｍ",L"占Ｎ", L"佔Ｎ",L"占Ｏ", L"佔Ｏ",L"占Ｐ", L"佔Ｐ",L"占Ｑ", L"佔Ｑ",L"占Ｒ", L"佔Ｒ",L"占Ｓ", L"佔Ｓ",L"占Ｔ", L"佔Ｔ",L"占Ｕ", L"佔Ｕ",L"占Ｖ", L"佔Ｖ",L"占Ｗ", L"佔Ｗ",L"占Ｘ", L"佔Ｘ",L"占Ｙ", L"佔Ｙ",L"占Ｚ", L"佔Ｚ",L"占ａ", L"佔ａ",L"占ｂ", L"佔ｂ",L"占ｃ", L"佔ｃ",L"占ｄ", L"佔ｄ",L"占ｅ", L"佔ｅ",L"占ｆ", L"佔ｆ",L"占ｇ", L"佔ｇ",L"占ｈ", L"佔ｈ",L"占ｉ", L"佔ｉ",L"占ｊ", L"佔ｊ",L"占ｋ", L"佔ｋ",L"占ｌ", L"佔ｌ",L"占ｍ", L"佔ｍ",L"占ｎ", L"佔ｎ",L"占ｏ", L"佔ｏ",L"占ｐ", L"佔ｐ",L"占ｑ", L"佔ｑ",L"占ｒ", L"佔ｒ",L"占ｓ", L"佔ｓ",L"占ｔ", L"佔ｔ",L"占ｕ", L"佔ｕ",L"占ｖ", L"佔ｖ",L"占ｗ", L"佔ｗ",L"占ｘ", L"佔ｘ",L"占ｙ", L"佔ｙ",L"占ｚ", L"佔ｚ",L"余光中", L"余光中",L"余光生", L"余光生",L"佛罗棱萨", L"佛羅稜薩",L"佛钟", L"佛鐘",L"作品里", L"作品裡",L"作奸犯科", L"作姦犯科",L"作准", L"作準",L"作庄", L"作莊",L"你克制", L"你剋制",L"你斗了胆", L"你斗了膽",L"你才子发昏", L"你纔子發昏",L"佣金收益", L"佣金收益",L"佣金费用", L"佣金費用",L"佳肴", L"佳肴",L"并一不二", L"併一不二",L"并入", L"併入",L"并兼", L"併兼",L"并到", L"併到",L"并合", L"併合",L"并名", L"併名",L"并吞下", L"併吞下",L"并拢", L"併攏",L"并案", L"併案",L"并流", L"併流",L"并火", L"併火",L"并为一家", L"併為一家",L"并为一体", L"併為一體",L"并产", L"併產",L"并当", L"併當",L"并叠", L"併疊",L"并发", L"併發",L"并科", L"併科",L"并网", L"併網",L"并线", L"併線",L"并肩子", L"併肩子",L"并购", L"併購",L"并除", L"併除",L"并骨", L"併骨",L"使其斗", L"使其鬥",L"来于", L"來於",L"来复", L"來複",L"侍仆", L"侍僕",L"供制", L"供製",L"依依不舍", L"依依不捨",L"依托", L"依託",L"侵占", L"侵佔",L"侵并", L"侵併",L"侵占到", L"侵占到",L"侵占罪", L"侵占罪",L"便药", L"便藥",L"系数", L"係數",L"系为", L"係為",L"俄占", L"俄佔",L"保险柜", L"保險柜",L"信托贸易", L"信托貿易",L"信托", L"信託",L"修改后", L"修改後",L"修杰楷", L"修杰楷",L"修炼", L"修鍊",L"修胡刀", L"修鬍刀",L"俯冲", L"俯衝",L"个人", L"個人",L"个里", L"個裡",L"个钟", L"個鐘",L"个钟表", L"個鐘錶",L"们克制", L"們剋制",L"们斗了胆", L"們斗了膽",L"倒绷孩儿", L"倒繃孩兒",L"幸免", L"倖免",L"幸存", L"倖存",L"幸幸", L"倖幸",L"倛丑", L"倛醜",L"借听于聋", L"借聽於聾",L"倦游", L"倦遊",L"假药", L"假藥",L"假托", L"假託",L"假发", L"假髮",L"偎干", L"偎乾",L"偏后", L"偏後",L"做庄", L"做莊",L"停停当当", L"停停當當",L"停征", L"停徵",L"停制", L"停製",L"偷鸡不着", L"偷雞不著",L"伪药", L"偽藥",L"备注", L"備註",L"家伙", L"傢伙",L"家俱", L"傢俱",L"家具", L"傢具",L"催并", L"催併",L"佣中佼佼", L"傭中佼佼",L"佣人", L"傭人",L"佣仆", L"傭僕",L"佣兵", L"傭兵",L"佣工", L"傭工",L"佣懒", L"傭懶",L"佣书", L"傭書",L"佣金", L"傭金",L"傲霜斗雪", L"傲霜鬥雪",L"传位于四太子", L"傳位于四太子",L"传于", L"傳於",L"伤痕累累", L"傷痕纍纍",L"傻里傻气", L"傻裡傻氣",L"倾复", L"傾複",L"仆人", L"僕人",L"仆使", L"僕使",L"仆仆", L"僕僕",L"仆僮", L"僕僮",L"仆吏", L"僕吏",L"仆固怀恩", L"僕固懷恩",L"仆夫", L"僕夫",L"仆姑", L"僕姑",L"仆妇", L"僕婦",L"仆射", L"僕射",L"仆少", L"僕少",L"仆役", L"僕役",L"仆从", L"僕從",L"仆憎", L"僕憎",L"仆欧", L"僕歐",L"仆程", L"僕程",L"仆虽罢驽", L"僕雖罷駑",L"侥幸", L"僥倖",L"僮仆", L"僮僕",L"雇主", L"僱主",L"雇人", L"僱人",L"雇到", L"僱到",L"雇员", L"僱員",L"雇工", L"僱工",L"雇用", L"僱用",L"雇农", L"僱農",L"仪范", L"儀範",L"仪表", L"儀錶",L"亿个", L"億個",L"亿多只", L"億多隻",L"亿天后", L"億天後",L"亿只", L"億隻",L"亿余", L"億餘",L"俭仆", L"儉僕",L"俭朴", L"儉樸",L"俭确之教", L"儉确之教",L"儒略改革历", L"儒略改革曆",L"儒略改革历史", L"儒略改革歷史",L"儒略历", L"儒略曆",L"儒略历史", L"儒略歷史",L"尽尽", L"儘儘",L"尽先", L"儘先",L"尽其所有", L"儘其所有",L"尽力", L"儘力",L"尽可能", L"儘可能",L"尽快", L"儘快",L"尽早", L"儘早",L"尽是", L"儘是",L"尽管", L"儘管",L"尽速", L"儘速",L"优于", L"優於",L"优游", L"優遊",L"兀术", L"兀朮",L"元凶", L"元兇",L"充饥", L"充饑",L"兆个", L"兆個",L"兆余", L"兆餘",L"凶刀", L"兇刀",L"凶器", L"兇器",L"凶嫌", L"兇嫌",L"凶巴巴", L"兇巴巴",L"凶徒", L"兇徒",L"凶悍", L"兇悍",L"凶恶", L"兇惡",L"凶手", L"兇手",L"凶案", L"兇案",L"凶枪", L"兇槍",L"凶横", L"兇橫",L"凶殘", L"兇殘",L"凶残", L"兇殘",L"凶殺", L"兇殺",L"凶杀", L"兇殺",L"凶犯", L"兇犯",L"凶狠", L"兇狠",L"凶猛", L"兇猛",L"凶疑", L"兇疑",L"凶相", L"兇相",L"凶险", L"兇險",L"先占", L"先佔",L"先后", L"先後",L"先忧后乐", L"先憂後樂",L"先采", L"先採",L"先攻后守", L"先攻後守",L"先盛后衰", L"先盛後衰",L"先礼后兵", L"先禮後兵",L"先义后利", L"先義後利",L"先声后实", L"先聲後實",L"先苦后甘", L"先苦後甘",L"先赢后输", L"先贏後輸",L"先进后出", L"先進後出",L"先开花后结果", L"先開花後結果",L"光前裕后", L"光前裕後",L"光致致", L"光緻緻",L"克药", L"克藥",L"克复", L"克複",L"免征", L"免徵",L"党参", L"党參",L"党太尉", L"党太尉",L"党怀英", L"党懷英",L"党进", L"党進",L"党项", L"党項",L"入夜后", L"入夜後",L"内制", L"內製",L"内面包", L"內面包",L"内面包的", L"內面包的",L"内斗", L"內鬥",L"内哄", L"內鬨",L"全干", L"全乾",L"全面包围", L"全面包圍",L"全面包裹", L"全面包裹",L"两个", L"兩個",L"两天后", L"兩天後",L"两天晒网", L"兩天晒網",L"两扎", L"兩紮",L"两虎共斗", L"兩虎共鬥",L"两只", L"兩隻",L"两余", L"兩餘",L"两鼠斗穴", L"兩鼠鬥穴",L"八个", L"八個",L"八出刊", L"八出刊",L"八出口", L"八出口",L"八出版", L"八出版",L"八出生", L"八出生",L"八出祁山", L"八出祁山",L"八出逃", L"八出逃",L"八大胡同", L"八大胡同",L"八天后", L"八天後",L"八字胡", L"八字鬍",L"八扎", L"八紮",L"八蜡", L"八蜡",L"八只", L"八隻",L"八余", L"八餘",L"公仔面", L"公仔麵",L"公仆", L"公僕",L"公元后", L"公元後",L"公孙丑", L"公孫丑",L"公干", L"公幹",L"公历", L"公曆",L"公历史", L"公歷史",L"公厘", L"公釐",L"公余", L"公餘",L"六个", L"六個",L"六出刊", L"六出刊",L"六出口", L"六出口",L"六出版", L"六出版",L"六出生", L"六出生",L"六出祁山", L"六出祁山",L"六出逃", L"六出逃",L"六划", L"六劃",L"六天后", L"六天後",L"六谷", L"六穀",L"六扎", L"六紮",L"六冲", L"六衝",L"六只", L"六隻",L"六余", L"六餘",L"六出", L"六齣",L"共和历", L"共和曆",L"共和历史", L"共和歷史",L"其一只", L"其一只",L"其二只", L"其二只",L"其八九只", L"其八九只",L"其后", L"其後",L"其次辟地", L"其次辟地",L"其余", L"其餘",L"典范", L"典範",L"兼并", L"兼并",L"冉有仆", L"冉有僕",L"冗余", L"冗餘",L"冤仇", L"冤讎",L"冥蒙", L"冥濛",L"冬天里", L"冬天裡",L"冬山庄", L"冬山庄",L"冬日里", L"冬日裡",L"冬游", L"冬遊",L"冶游", L"冶遊",L"冷庄子", L"冷莊子",L"冷面相", L"冷面相",L"冷面", L"冷麵",L"准不准他", L"准不准他",L"准不准你", L"准不准你",L"准不准她", L"准不准她",L"准不准它", L"准不准它",L"准不准我", L"准不准我",L"准不准许", L"准不准許",L"准不准谁", L"准不准誰",L"准保护", L"准保護",L"准保释", L"准保釋",L"凌蒙初", L"凌濛初",L"凝炼", L"凝鍊",L"几上", L"几上",L"几几", L"几几",L"几凳", L"几凳",L"几子", L"几子",L"几旁", L"几旁",L"几杖", L"几杖",L"几案", L"几案",L"几椅", L"几椅",L"几榻", L"几榻",L"几净窗明", L"几淨窗明",L"几筵", L"几筵",L"几丝", L"几絲",L"几面上", L"几面上",L"凶杀案", L"凶殺案",L"凶相毕露", L"凶相畢露",L"凹洞里", L"凹洞裡",L"出乖弄丑", L"出乖弄醜",L"出乖露丑", L"出乖露醜",L"出征收", L"出征收",L"出于", L"出於",L"出谋划策", L"出謀劃策",L"出游", L"出遊",L"出丑", L"出醜",L"出锤", L"出鎚",L"分占", L"分佔",L"分别致", L"分别致",L"分半钟", L"分半鐘",L"分多钟", L"分多鐘",L"分子钟", L"分子鐘",L"分布圖", L"分布圖",L"分布图", L"分布圖",L"分布于", L"分布於",L"分散于", L"分散於",L"分钟", L"分鐘",L"刑余", L"刑餘",L"划一桨", L"划一槳",L"划了一会", L"划了一會",L"划来划去", L"划來划去",L"划到岸", L"划到岸",L"划到江心", L"划到江心",L"划得来", L"划得來",L"划着", L"划著",L"划着走", L"划著走",L"划龙舟", L"划龍舟",L"判断发", L"判斷發",L"别后", L"別後",L"别日南鸿才北去", L"別日南鴻纔北去",L"别致", L"別緻",L"别庄", L"別莊",L"别着", L"別著",L"别辟", L"別闢",L"利欲", L"利慾",L"利于", L"利於",L"利欲熏心", L"利欲熏心",L"删后留位", L"刪後留位",L"删后缩位", L"刪後縮位",L"刮来刮去", L"刮來刮去",L"刮着", L"刮著",L"刮起来", L"刮起來",L"刮风下雪倒便宜", L"刮風下雪倒便宜",L"刮胡", L"刮鬍",L"制冷机", L"制冷機",L"制签", L"制籤",L"制钟", L"制鐘",L"刺绣", L"刺繡",L"刻划", L"刻劃",L"刻半钟", L"刻半鐘",L"刻多钟", L"刻多鐘",L"刻钟", L"刻鐘",L"剃发", L"剃髮",L"剃胡", L"剃鬍",L"剃须", L"剃鬚",L"削发", L"削髮",L"削面", L"削麵",L"克制不了", L"剋制不了",L"克制不住", L"剋制不住",L"克扣", L"剋扣",L"克星", L"剋星",L"克期", L"剋期",L"克死", L"剋死",L"克薄", L"剋薄",L"前仰后合", L"前仰後合",L"前倨后恭", L"前倨後恭",L"前前后后", L"前前後後",L"前呼后拥", L"前呼後擁",L"前后", L"前後",L"前思后想", L"前思後想",L"前挽后推", L"前挽後推",L"前短后长", L"前短後長",L"前言不对后语", L"前言不對後語",L"前言不答后语", L"前言不答後語",L"前面店", L"前面店",L"剔庄货", L"剔莊貨",L"刚干", L"剛乾",L"刚雇", L"剛僱",L"刚才一载", L"剛纔一載",L"剥制", L"剝製",L"剩余", L"剩餘",L"剪其发", L"剪其髮",L"剪牡丹喂牛", L"剪牡丹喂牛",L"剪彩", L"剪綵",L"剪发", L"剪髮",L"割舍", L"割捨",L"创获", L"創穫",L"创制", L"創製",L"铲出", L"剷出",L"铲刈", L"剷刈",L"铲平", L"剷平",L"铲除", L"剷除",L"铲头", L"剷頭",L"划一", L"劃一",L"划上", L"劃上",L"划下", L"劃下",L"划了", L"劃了",L"划出", L"劃出",L"划分", L"劃分",L"划到", L"劃到",L"划划", L"劃劃",L"划去", L"劃去",L"划在", L"劃在",L"划地", L"劃地",L"划定", L"劃定",L"划得", L"劃得",L"划成", L"劃成",L"划掉", L"劃掉",L"划拨", L"劃撥",L"划时代", L"劃時代",L"划款", L"劃款",L"划归", L"劃歸",L"划法", L"劃法",L"划清", L"劃清",L"划为", L"劃為",L"划界", L"劃界",L"划破", L"劃破",L"划线", L"劃線",L"划足", L"劃足",L"划过", L"劃過",L"划开", L"劃開",L"剧药", L"劇藥",L"刘克庄", L"劉克莊",L"力克制", L"力剋制",L"力拼", L"力拚",L"力拼众敌", L"力拼眾敵",L"力求克制", L"力求剋制",L"力争上游", L"力爭上遊",L"功致", L"功緻",L"加氢精制", L"加氫精制",L"加药", L"加藥",L"加注", L"加註",L"劣于", L"劣於",L"助于", L"助於",L"劫后余生", L"劫後餘生",L"劫余", L"劫餘",L"勃郁", L"勃鬱",L"动荡", L"動蕩",L"胜于", L"勝於",L"劳力士表", L"勞力士錶",L"勤仆", L"勤僕",L"勤朴", L"勤樸",L"勋章", L"勳章",L"勺药", L"勺藥",L"勾干", L"勾幹",L"勾心斗角", L"勾心鬥角",L"勾魂荡魄", L"勾魂蕩魄",L"包括", L"包括",L"包准", L"包準",L"包谷", L"包穀",L"包扎", L"包紮",L"包庄", L"包莊",L"匏系", L"匏繫",L"北岳", L"北嶽",L"北回线", L"北迴線",L"北回铁路", L"北迴鐵路",L"匡复", L"匡複",L"匪干", L"匪幹",L"匿于", L"匿於",L"区划", L"區劃",L"十个", L"十個",L"十出刊", L"十出刊",L"十出口", L"十出口",L"十出版", L"十出版",L"十出生", L"十出生",L"十出祁山", L"十出祁山",L"十出逃", L"十出逃",L"十划", L"十劃",L"十多只", L"十多隻",L"十天后", L"十天後",L"十扎", L"十紮",L"十只", L"十隻",L"十余", L"十餘",L"十出", L"十齣",L"千个", L"千個",L"千只可", L"千只可",L"千只够", L"千只夠",L"千只怕", L"千只怕",L"千只能", L"千只能",L"千只足够", L"千只足夠",L"千多只", L"千多隻",L"千天后", L"千天後",L"千扎", L"千紮",L"千丝万缕", L"千絲萬縷",L"千回百折", L"千迴百折",L"千回百转", L"千迴百轉",L"千钧一发", L"千鈞一髮",L"千只", L"千隻",L"千余", L"千餘",L"升官发财", L"升官發財",L"午后", L"午後",L"半制品", L"半制品",L"半只可", L"半只可",L"半只够", L"半只夠",L"半于", L"半於",L"半只", L"半隻",L"南京钟", L"南京鐘",L"南京钟表", L"南京鐘錶",L"南宫适", L"南宮适",L"南屏晚钟", L"南屏晚鐘",L"南岳", L"南嶽",L"南筑", L"南筑",L"南回线", L"南迴線",L"南回铁路", L"南迴鐵路",L"南游", L"南遊",L"博汇", L"博彙",L"博采", L"博採",L"卞庄", L"卞莊",L"卞庄子", L"卞莊子",L"占了卜", L"占了卜",L"占便宜的是呆", L"占便宜的是獃",L"占卜", L"占卜",L"占多数", L"占多數",L"占有五不验", L"占有五不驗",L"占有权", L"占有權",L"印累绶若", L"印纍綬若",L"印制", L"印製",L"危于", L"危於",L"卵与石斗", L"卵與石鬥",L"卷须", L"卷鬚",L"厂部", L"厂部",L"厝薪于火", L"厝薪於火",L"原子钟", L"原子鐘",L"原钟", L"原鐘",L"历物之意", L"厤物之意",L"厥后", L"厥後",L"参合", L"參合",L"参考价值", L"參考價值",L"参与", L"參與",L"参与人员", L"參與人員",L"参与制", L"參與制",L"参与感", L"參與感",L"参与者", L"參與者",L"参观团", L"參觀團",L"参观团体", L"參觀團體",L"参阅", L"參閱",L"反朴", L"反樸",L"反冲", L"反衝",L"反复制", L"反複製",L"反复", L"反覆",L"反覆", L"反覆",L"取舍", L"取捨",L"受托", L"受託",L"口干", L"口乾",L"口干冒", L"口干冒",L"口干政", L"口干政",L"口干涉", L"口干涉",L"口干犯", L"口干犯",L"口干预", L"口干預",L"口燥唇干", L"口燥唇乾",L"口腹之欲", L"口腹之慾",L"口里", L"口裡",L"口钟", L"口鐘",L"古书云", L"古書云",L"古書云", L"古書云",L"古柯咸", L"古柯鹹",L"古朴", L"古樸",L"古语云", L"古語云",L"古語云", L"古語云",L"古迹", L"古迹",L"古钟", L"古鐘",L"古钟表", L"古鐘錶",L"另辟", L"另闢",L"叩钟", L"叩鐘",L"只占", L"只佔",L"只占卜", L"只占卜",L"只占吉", L"只占吉",L"只占神问卜", L"只占神問卜",L"只占算", L"只占算",L"只采", L"只採",L"只冲", L"只衝",L"只身上已", L"只身上已",L"只身上有", L"只身上有",L"只身上没", L"只身上沒",L"只身上无", L"只身上無",L"只身上的", L"只身上的",L"只身世", L"只身世",L"只身份", L"只身份",L"只身前", L"只身前",L"只身受", L"只身受",L"只身子", L"只身子",L"只身形", L"只身形",L"只身影", L"只身影",L"只身后", L"只身後",L"只身心", L"只身心",L"只身旁", L"只身旁",L"只身材", L"只身材",L"只身段", L"只身段",L"只身为", L"只身為",L"只身边", L"只身邊",L"只身首", L"只身首",L"只身体", L"只身體",L"只身高", L"只身高",L"只采声", L"只采聲",L"叮叮当当", L"叮叮噹噹",L"叮当", L"叮噹",L"可以克制", L"可以剋制",L"可紧可松", L"可緊可鬆",L"可自制", L"可自制",L"台子女", L"台子女",L"台子孙", L"台子孫",L"台布景", L"台布景",L"台后", L"台後",L"台历史", L"台歷史",L"台钟", L"台鐘",L"台面前", L"台面前",L"叱咤903", L"叱咤903",L"叱咤MY903", L"叱咤MY903",L"叱咤My903", L"叱咤My903",L"叱咤叱叱咤", L"叱咤叱叱咤",L"叱咤叱咤叱咤咤", L"叱咤叱咤叱咤咤",L"叱咤咤", L"叱咤咤",L"叱咤乐坛", L"叱咤樂壇",L"叱咤樂壇", L"叱咤樂壇",L"右后", L"右後",L"叶 恭弘", L"叶 恭弘",L"叶　恭弘", L"叶　恭弘",L"叶恭弘", L"叶恭弘",L"叶音", L"叶音",L"叶韵", L"叶韻",L"吃板刀面", L"吃板刀麵",L"吃着不尽", L"吃著不盡",L"吃姜", L"吃薑",L"吃药", L"吃藥",L"吃药后", L"吃藥後",L"吃里扒外", L"吃裡扒外",L"吃里爬外", L"吃裡爬外",L"吃辣面", L"吃辣麵",L"吃错药", L"吃錯藥",L"各辟", L"各闢",L"各类钟", L"各類鐘",L"合伙人", L"合伙人",L"合并", L"合併",L"合伙", L"合夥",L"合府上", L"合府上",L"合采", L"合採",L"合历", L"合曆",L"合历史", L"合歷史",L"合准", L"合準",L"合着", L"合著",L"合著者", L"合著者",L"吉凶庆吊", L"吉凶慶弔",L"吊带裤", L"吊帶褲",L"吊挂着", L"吊掛著",L"吊杆", L"吊杆",L"吊着", L"吊著",L"吊裤", L"吊褲",L"吊裤带", L"吊褲帶",L"吊钟", L"吊鐘",L"同伙", L"同夥",L"同于", L"同於",L"同余", L"同餘",L"后丰", L"后豐",L"后豐", L"后豐",L"后发座", L"后髮座",L"吐哺捉发", L"吐哺捉髮",L"吐哺握发", L"吐哺握髮",L"向往来", L"向往來",L"向往常", L"向往常",L"向往日", L"向往日",L"向往时", L"向往時",L"向后", L"向後",L"向着", L"向著",L"吞并", L"吞併",L"吟游", L"吟遊",L"含齿戴发", L"含齒戴髮",L"吹干", L"吹乾",L"吹发", L"吹髮",L"吹胡", L"吹鬍",L"吾为之范我驰驱", L"吾爲之範我馳驅",L"呆呆傻傻", L"呆呆傻傻",L"呆呆挣挣", L"呆呆掙掙",L"呆呆兽", L"呆呆獸",L"呆呆笨笨", L"呆呆笨笨",L"呆致致", L"呆緻緻",L"呆里呆气", L"呆裡呆氣",L"周一", L"周一",L"周三", L"周三",L"周二", L"周二",L"周五", L"周五",L"周六", L"周六",L"周四", L"周四",L"周历", L"周曆",L"周杰伦", L"周杰倫",L"周杰倫", L"周杰倫",L"周历史", L"周歷史",L"周庄王", L"周莊王",L"周游", L"周遊",L"呼吁", L"呼籲",L"命中注定", L"命中注定",L"和克制", L"和剋制",L"和奸", L"和姦",L"咎征", L"咎徵",L"咕咕钟", L"咕咕鐘",L"咬姜呷醋", L"咬薑呷醋",L"咯当", L"咯噹",L"咳嗽药", L"咳嗽藥",L"哀吊", L"哀弔",L"哀挽", L"哀輓",L"品汇", L"品彙",L"哄堂大笑", L"哄堂大笑",L"员山庄", L"員山庄",L"哪里", L"哪裡",L"哭脏", L"哭髒",L"唁吊", L"唁弔",L"呗赞", L"唄讚",L"唇干", L"唇乾",L"售后", L"售後",L"唯一只", L"唯一只",L"唱游", L"唱遊",L"唾面自干", L"唾面自乾",L"唾余", L"唾餘",L"商历", L"商曆",L"商历史", L"商歷史",L"啷当", L"啷噹",L"喂了一声", L"喂了一聲",L"善后", L"善後",L"善于", L"善於",L"喜向往", L"喜向往",L"喜欢表", L"喜歡錶",L"喜欢钟", L"喜歡鐘",L"喜欢钟表", L"喜歡鐘錶",L"喝干", L"喝乾",L"喧哄", L"喧鬨",L"丧钟", L"喪鐘",L"乔岳", L"喬嶽",L"单于", L"單于",L"单单于", L"單單於",L"单干", L"單幹",L"单打独斗", L"單打獨鬥",L"单只", L"單隻",L"嗑药", L"嗑藥",L"嗣后", L"嗣後",L"嘀嗒的表", L"嘀嗒的錶",L"嘉谷", L"嘉穀",L"嘉肴", L"嘉肴",L"嘴里", L"嘴裡",L"恶心", L"噁心",L"噙齿戴发", L"噙齒戴髮",L"喷洒", L"噴洒",L"当啷", L"噹啷",L"当当", L"噹噹",L"噜苏", L"嚕囌",L"向导", L"嚮導",L"向往", L"嚮往",L"向应", L"嚮應",L"向迩", L"嚮邇",L"严于", L"嚴於",L"严丝合缝", L"嚴絲合縫",L"嚼谷", L"嚼穀",L"囉囉苏苏", L"囉囉囌囌",L"囉苏", L"囉囌",L"嘱托", L"囑託",L"四个", L"四個",L"四出刊", L"四出刊",L"四出口", L"四出口",L"四出征收", L"四出徵收",L"四出版", L"四出版",L"四出生", L"四出生",L"四出祁山", L"四出祁山",L"四出逃", L"四出逃",L"四分历", L"四分曆",L"四分历史", L"四分歷史",L"四天后", L"四天後",L"四舍五入", L"四捨五入",L"四扎", L"四紮",L"四只", L"四隻",L"四面包", L"四面包",L"四面钟", L"四面鐘",L"四余", L"四餘",L"四出", L"四齣",L"回采", L"回採",L"回旋加速", L"回旋加速",L"回历", L"回曆",L"回历史", L"回歷史",L"回丝", L"回絲",L"回着", L"回著",L"回荡", L"回蕩",L"回游", L"回遊",L"回阳荡气", L"回陽蕩氣",L"因于", L"因於",L"困倦起来", L"困倦起來",L"困兽之斗", L"困獸之鬥",L"困兽犹斗", L"困獸猶鬥",L"困斗", L"困鬥",L"固征", L"固徵",L"囿于", L"囿於",L"圈占", L"圈佔",L"圈子里", L"圈子裡",L"圈梁", L"圈樑",L"圈里", L"圈裡",L"国之桢干", L"國之楨榦",L"国于", L"國於",L"国历", L"國曆",L"国历代", L"國歷代",L"国历任", L"國歷任",L"国历史", L"國歷史",L"国历届", L"國歷屆",L"国仇", L"國讎",L"园里", L"園裡",L"园游会", L"園遊會",L"图里", L"圖裡",L"图鉴", L"圖鑑",L"土里", L"土裡",L"土制", L"土製",L"土霉素", L"土霉素",L"在制品", L"在制品",L"在克制", L"在剋制",L"在后", L"在後",L"在于", L"在於",L"地占", L"地佔",L"地克制", L"地剋制",L"地方志", L"地方志",L"地志", L"地誌",L"地丑德齐", L"地醜德齊",L"坏于", L"坏於",L"坐如钟", L"坐如鐘",L"坐庄", L"坐莊",L"坐钟", L"坐鐘",L"坑里", L"坑裡",L"坤范", L"坤範",L"坦荡", L"坦蕩",L"坦荡荡", L"坦蕩蕩",L"坱郁", L"坱鬱",L"垂于", L"垂於",L"垂范", L"垂範",L"垂发", L"垂髮",L"型范", L"型範",L"埃及历", L"埃及曆",L"埃及历史", L"埃及歷史",L"埃荣冲", L"埃榮衝",L"埋头寻表", L"埋頭尋錶",L"埋头寻钟", L"埋頭尋鐘",L"埋头寻钟表", L"埋頭尋鐘錶",L"城里", L"城裡",L"基干", L"基幹",L"基于", L"基於",L"基准", L"基準",L"坚致", L"堅緻",L"堙淀", L"堙澱",L"涂着", L"塗著",L"涂药", L"塗藥",L"塞耳盗钟", L"塞耳盜鐘",L"塞药", L"塞藥",L"墓志铭", L"墓志銘",L"墓志", L"墓誌",L"增辟", L"增闢",L"墨沈", L"墨沈",L"墨沈未干", L"墨瀋未乾",L"堕胎药", L"墮胎藥",L"垦复", L"墾複",L"垦辟", L"墾闢",L"垄断价格", L"壟斷價格",L"垄断资产", L"壟斷資產",L"垄断集团", L"壟斷集團",L"壮游", L"壯遊",L"壮面", L"壯麵",L"壹郁", L"壹鬱",L"壶里", L"壺裡",L"壸范", L"壼範",L"寿面", L"壽麵",L"夏天里", L"夏天裡",L"夏日里", L"夏日裡",L"夏历", L"夏曆",L"夏历史", L"夏歷史",L"夏游", L"夏遊",L"外强中干", L"外強中乾",L"外制", L"外製",L"多占", L"多佔",L"多划", L"多劃",L"多半只", L"多半只",L"多只是", L"多只是",L"多只有", L"多只有",L"多只能", L"多只能",L"多只需", L"多只需",L"多天后", L"多天後",L"多于", L"多於",L"多冲", L"多衝",L"多丑", L"多醜",L"多只", L"多隻",L"多余", L"多餘",L"多么", L"多麼",L"夜光表", L"夜光錶",L"夜里", L"夜裡",L"夜游", L"夜遊",L"够克制", L"夠剋制",L"梦有五不占", L"夢有五不占",L"梦里", L"夢裡",L"梦游", L"夢遊",L"伙伴", L"夥伴",L"伙友", L"夥友",L"伙同", L"夥同",L"伙众", L"夥眾",L"伙计", L"夥計",L"大丑", L"大丑",L"大伙儿", L"大伙兒",L"大型钟", L"大型鐘",L"大型钟表面", L"大型鐘表面",L"大型钟表", L"大型鐘錶",L"大型钟面", L"大型鐘面",L"大伙", L"大夥",L"大干", L"大幹",L"大批涌到", L"大批湧到",L"大折儿", L"大摺兒",L"大明历", L"大明曆",L"大明历史", L"大明歷史",L"大历", L"大曆",L"大本钟", L"大本鐘",L"大本钟敲", L"大本鐘敲",L"大历史", L"大歷史",L"大呆", L"大獃",L"大病初愈", L"大病初癒",L"大目干连", L"大目乾連",L"大笨钟", L"大笨鐘",L"大笨钟敲", L"大笨鐘敲",L"大蜡", L"大蜡",L"大衍历", L"大衍曆",L"大衍历史", L"大衍歷史",L"大言非夸", L"大言非夸",L"大赞", L"大讚",L"大周折", L"大週摺",L"大金发苔", L"大金髮苔",L"大锤", L"大鎚",L"大钟", L"大鐘",L"大只", L"大隻",L"大曲", L"大麴",L"天干物燥", L"天乾物燥",L"天克地冲", L"天克地衝",L"天后宫", L"天后宮",L"天后庙道", L"天后廟道",L"天地志狼", L"天地志狼",L"天地为范", L"天地為範",L"天干地支", L"天干地支",L"天后", L"天後",L"天文学钟", L"天文學鐘",L"天文钟", L"天文鐘",L"天翻地覆", L"天翻地覆",L"天覆地载", L"天覆地載",L"太仆", L"太僕",L"太初历", L"太初曆",L"太初历史", L"太初歷史",L"夯干", L"夯幹",L"夸人", L"夸人",L"夸克", L"夸克",L"夸夸其谈", L"夸夸其談",L"夸姣", L"夸姣",L"夸容", L"夸容",L"夸毗", L"夸毗",L"夸父", L"夸父",L"夸特", L"夸特",L"夸脱", L"夸脫",L"夸诞", L"夸誕",L"夸诞不经", L"夸誕不經",L"夸丽", L"夸麗",L"奇迹", L"奇迹",L"奇丑", L"奇醜",L"奏折", L"奏摺",L"奥占", L"奧佔",L"夺斗", L"奪鬥",L"奋斗", L"奮鬥",L"女丑", L"女丑",L"女佣人", L"女佣人",L"女佣", L"女傭",L"女仆", L"女僕",L"奴仆", L"奴僕",L"奸淫掳掠", L"奸淫擄掠",L"她克制", L"她剋制",L"好干", L"好乾",L"好家伙", L"好傢夥",L"好勇斗狠", L"好勇鬥狠",L"好斗大", L"好斗大",L"好斗室", L"好斗室",L"好斗笠", L"好斗笠",L"好斗篷", L"好斗篷",L"好斗胆", L"好斗膽",L"好斗蓬", L"好斗蓬",L"好于", L"好於",L"好呆", L"好獃",L"好困", L"好睏",L"好签", L"好籤",L"好丑", L"好醜",L"好斗", L"好鬥",L"如果干", L"如果幹",L"如饥似渴", L"如饑似渴",L"妙药", L"妙藥",L"始于", L"始於",L"委托", L"委託",L"委托书", L"委託書",L"奸夫", L"姦夫",L"奸妇", L"姦婦",L"奸宄", L"姦宄",L"奸情", L"姦情",L"奸杀", L"姦殺",L"奸污", L"姦汙",L"奸淫", L"姦淫",L"奸猾", L"姦猾",L"奸细", L"姦細",L"奸邪", L"姦邪",L"威棱", L"威稜",L"婚后", L"婚後",L"婢仆", L"婢僕",L"娲杆", L"媧杆",L"嫁祸于", L"嫁禍於",L"嫌凶", L"嫌兇",L"嫌好道丑", L"嫌好道醜",L"嬉游", L"嬉遊",L"嬖幸", L"嬖倖",L"嬴余", L"嬴餘",L"子之丰兮", L"子之丰兮",L"子云", L"子云",L"字汇", L"字彙",L"字码表", L"字碼表",L"字里行间", L"字裡行間",L"存十一于千百", L"存十一於千百",L"存折", L"存摺",L"存于", L"存於",L"季后赛", L"季後賽",L"孤寡不谷", L"孤寡不穀",L"宇宙志", L"宇宙誌",L"守先待后", L"守先待後",L"安于", L"安於",L"安沈铁路", L"安瀋鐵路",L"安眠药", L"安眠藥",L"安胎药", L"安胎藥",L"完工后", L"完工後",L"完成后", L"完成後",L"宗周钟", L"宗周鐘",L"官不怕大只怕管", L"官不怕大只怕管",L"官地为采", L"官地為寀",L"官历", L"官曆",L"官历史", L"官歷史",L"官庄", L"官莊",L"定于", L"定於",L"定准", L"定準",L"定制", L"定製",L"宜云", L"宜云",L"宣泄", L"宣洩",L"宦游", L"宦遊",L"宫里", L"宮裡",L"害于", L"害於",L"宴游", L"宴遊",L"家仆", L"家僕",L"家具备", L"家具備",L"家具有", L"家具有",L"家具木工科", L"家具木工科",L"家具行", L"家具行",L"家具体", L"家具體",L"家庄", L"家莊",L"家里", L"家裡",L"家丑", L"家醜",L"容后说明", L"容後說明",L"容于", L"容於",L"容范", L"容範",L"寄托在", L"寄托在",L"寄托", L"寄託",L"密致", L"密緻",L"寇准", L"寇準",L"寇仇", L"寇讎",L"富余", L"富餘",L"寒假里", L"寒假裡",L"寒栗", L"寒慄",L"寒于", L"寒於",L"寓于", L"寓於",L"寡占", L"寡佔",L"寡欲", L"寡慾",L"实干", L"實幹",L"写字台", L"寫字檯",L"宽宽松松", L"寬寬鬆鬆",L"宽于", L"寬於",L"宽余", L"寬餘",L"宽松", L"寬鬆",L"寮采", L"寮寀",L"宝山庄", L"寶山庄",L"寶曆", L"寶曆",L"宝历", L"寶曆",L"宝历史", L"寶歷史",L"宝庄", L"寶莊",L"宝里宝气", L"寶裡寶氣",L"寸发千金", L"寸髮千金",L"寺钟", L"寺鐘",L"封面里", L"封面裡",L"射雕", L"射鵰",L"将占", L"將佔",L"将占卜", L"將占卜",L"专向往", L"專向往",L"专注", L"專註",L"专辑里", L"專輯裡",L"对折", L"對摺",L"对于", L"對於",L"对准", L"對準",L"对准表", L"對準錶",L"对准钟", L"對準鐘",L"对准钟表", L"對準鐘錶",L"对华发动", L"對華發動",L"对表中", L"對表中",L"对表扬", L"對表揚",L"对表明", L"對表明",L"对表演", L"對表演",L"对表现", L"對表現",L"对表达", L"對表達",L"对表", L"對錶",L"导游", L"導遊",L"小丑", L"小丑",L"小价", L"小价",L"小仆", L"小僕",L"小几", L"小几",L"小型钟", L"小型鐘",L"小型钟表面", L"小型鐘表面",L"小型钟表", L"小型鐘錶",L"小型钟面", L"小型鐘面",L"小伙子", L"小夥子",L"小米面", L"小米麵",L"小只", L"小隻",L"少占", L"少佔",L"少采", L"少採",L"就克制", L"就剋制",L"就范", L"就範",L"就里", L"就裡",L"尸位素餐", L"尸位素餐",L"尸利", L"尸利",L"尸居余气", L"尸居餘氣",L"尸祝", L"尸祝",L"尸禄", L"尸祿",L"尸臣", L"尸臣",L"尸谏", L"尸諫",L"尸魂界", L"尸魂界",L"尸鸠", L"尸鳩",L"局里", L"局裡",L"屁股大吊了心", L"屁股大弔了心",L"屋子里", L"屋子裡",L"屋梁", L"屋樑",L"屋里", L"屋裡",L"屑于", L"屑於",L"屡顾尔仆", L"屢顧爾僕",L"属于", L"屬於",L"属托", L"屬託",L"屯扎", L"屯紮",L"屯里", L"屯裡",L"山崩钟应", L"山崩鐘應",L"山岳", L"山嶽",L"山后", L"山後",L"山梁", L"山樑",L"山洞里", L"山洞裡",L"山棱", L"山稜",L"山羊胡", L"山羊鬍",L"山庄", L"山莊",L"山药", L"山藥",L"山里", L"山裡",L"山重水复", L"山重水複",L"岱岳", L"岱嶽",L"峰回", L"峰迴",L"峻岭", L"峻岭",L"昆剧", L"崑劇",L"昆山", L"崑山",L"昆仑", L"崑崙",L"昆仑山脉", L"崑崙山脈",L"昆曲", L"崑曲",L"昆腔", L"崑腔",L"昆苏", L"崑蘇",L"昆调", L"崑調",L"崖广", L"崖广",L"仑背", L"崙背",L"嶒棱", L"嶒稜",L"岳岳", L"嶽嶽",L"岳麓", L"嶽麓",L"川谷", L"川穀",L"巡回医疗", L"巡回醫療",L"巡回", L"巡迴",L"巡游", L"巡遊",L"工致", L"工緻",L"左后", L"左後",L"左冲右突", L"左衝右突",L"巧妇做不得无面馎饦", L"巧婦做不得無麵餺飥",L"巧干", L"巧幹",L"巧历", L"巧曆",L"巧历史", L"巧歷史",L"差之毫厘", L"差之毫厘",L"差之毫厘，谬以千里", L"差之毫釐，謬以千里",L"差于", L"差於",L"己丑", L"己丑",L"已占", L"已佔",L"已占卜", L"已占卜",L"已占算", L"已占算",L"巴尔干", L"巴爾幹",L"巷里", L"巷裡",L"市占", L"市佔",L"市占率", L"市佔率",L"市里", L"市裡",L"布谷", L"布穀",L"布谷鸟钟", L"布穀鳥鐘",L"布庄", L"布莊",L"布谷鸟", L"布谷鳥",L"希伯来历", L"希伯來曆",L"希伯来历史", L"希伯來歷史",L"帘子", L"帘子",L"帘布", L"帘布",L"师范", L"師範",L"席卷", L"席捲",L"带团参加", L"帶團參加",L"带征", L"帶徵",L"带发修行", L"帶髮修行",L"幕后", L"幕後",L"帮佣", L"幫傭",L"干系", L"干係",L"干着急", L"干著急",L"平平当当", L"平平當當",L"平泉庄", L"平泉莊",L"平准", L"平準",L"年代里", L"年代裡",L"年后", L"年後",L"年历", L"年曆",L"年历史", L"年歷史",L"年谷", L"年穀",L"年里", L"年裡",L"并力", L"并力",L"并吞", L"并吞",L"并州", L"并州",L"并日而食", L"并日而食",L"并行", L"并行",L"并迭", L"并迭",L"幸免于难", L"幸免於難",L"幸于", L"幸於",L"幸运胡", L"幸運鬍",L"干上", L"幹上",L"干下去", L"幹下去",L"干不了", L"幹不了",L"干不成", L"幹不成",L"干了", L"幹了",L"干事", L"幹事",L"干些", L"幹些",L"干人", L"幹人",L"干什么", L"幹什麼",L"干个", L"幹個",L"干劲", L"幹勁",L"干劲冲天", L"幹勁沖天",L"干吏", L"幹吏",L"干员", L"幹員",L"干啥", L"幹啥",L"干吗", L"幹嗎",L"干嘛", L"幹嘛",L"干坏事", L"幹壞事",L"干完", L"幹完",L"干家", L"幹家",L"干将", L"幹將",L"干得", L"幹得",L"干性油", L"幹性油",L"干才", L"幹才",L"干掉", L"幹掉",L"干探", L"幹探",L"干校", L"幹校",L"干活", L"幹活",L"干流", L"幹流",L"干济", L"幹濟",L"干营生", L"幹營生",L"干父之蛊", L"幹父之蠱",L"干球温度", L"幹球溫度",L"干甚么", L"幹甚麼",L"干略", L"幹略",L"干当", L"幹當",L"干的停当", L"幹的停當",L"干细胞", L"幹細胞",L"干細胞", L"幹細胞",L"干线", L"幹線",L"干练", L"幹練",L"干缺", L"幹缺",L"干群关系", L"幹群關係",L"干蛊", L"幹蠱",L"干警", L"幹警",L"干起来", L"幹起來",L"干路", L"幹路",L"干办", L"幹辦",L"干这一行", L"幹這一行",L"干这种事", L"幹這種事",L"干道", L"幹道",L"干部", L"幹部",L"干革命", L"幹革命",L"干头", L"幹頭",L"干么", L"幹麼",L"几划", L"幾劃",L"几天后", L"幾天後",L"几只", L"幾隻",L"几出", L"幾齣",L"广部", L"广部",L"庄稼人", L"庄稼人",L"庄稼院", L"庄稼院",L"店里", L"店裡",L"府干卿", L"府干卿",L"府干擾", L"府干擾",L"府干扰", L"府干擾",L"府干政", L"府干政",L"府干涉", L"府干涉",L"府干犯", L"府干犯",L"府干預", L"府干預",L"府干预", L"府干預",L"府干", L"府幹",L"府后", L"府後",L"座钟", L"座鐘",L"康庄大道", L"康庄大道",L"康采恩", L"康採恩",L"康庄", L"康莊",L"厨余", L"廚餘",L"厮斗", L"廝鬥",L"庙里", L"廟裡",L"广征", L"廣徵",L"广舍", L"廣捨",L"延后", L"延後",L"建于", L"建於",L"弄干", L"弄乾",L"弄丑", L"弄醜",L"弄脏", L"弄髒",L"弄松", L"弄鬆",L"弄鬼吊猴", L"弄鬼弔猴",L"吊儿郎当", L"弔兒郎當",L"吊卷", L"弔卷",L"吊取", L"弔取",L"吊古", L"弔古",L"吊古寻幽", L"弔古尋幽",L"吊唁", L"弔唁",L"吊问", L"弔問",L"吊喉", L"弔喉",L"吊丧", L"弔喪",L"吊丧问疾", L"弔喪問疾",L"吊喭", L"弔喭",L"吊场", L"弔場",L"吊奠", L"弔奠",L"吊孝", L"弔孝",L"吊客", L"弔客",L"吊宴", L"弔宴",L"吊带", L"弔帶",L"吊影", L"弔影",L"吊慰", L"弔慰",L"吊扣", L"弔扣",L"吊拷", L"弔拷",L"吊拷绷扒", L"弔拷繃扒",L"吊挂", L"弔掛",L"吊撒", L"弔撒",L"吊文", L"弔文",L"吊旗", L"弔旗",L"吊书", L"弔書",L"吊桥", L"弔橋",L"吊死", L"弔死",L"吊死问疾", L"弔死問疾",L"吊民", L"弔民",L"吊民伐罪", L"弔民伐罪",L"吊祭", L"弔祭",L"吊纸", L"弔紙",L"吊者大悦", L"弔者大悅",L"吊腰撒跨", L"弔腰撒跨",L"吊脚儿事", L"弔腳兒事",L"吊膀子", L"弔膀子",L"吊词", L"弔詞",L"吊诡", L"弔詭",L"吊诡矜奇", L"弔詭矜奇",L"吊谎", L"弔謊",L"吊贺迎送", L"弔賀迎送",L"吊头", L"弔頭",L"吊颈", L"弔頸",L"吊鹤", L"弔鶴",L"引斗", L"引鬥",L"弘历", L"弘曆",L"弘历史", L"弘歷史",L"弱于", L"弱於",L"弱水三千只取一瓢", L"弱水三千只取一瓢",L"张三丰", L"張三丰",L"張三丰", L"張三丰",L"张勋", L"張勳",L"强占", L"強佔",L"强制作用", L"強制作用",L"强奸", L"強姦",L"强干", L"強幹",L"强于", L"強於",L"别口气", L"彆口氣",L"别强", L"彆強",L"别扭", L"彆扭",L"别拗", L"彆拗",L"别气", L"彆氣",L"弹子台", L"彈子檯",L"弹珠台", L"彈珠檯",L"弹药", L"彈藥",L"汇刊", L"彙刊",L"汇报", L"彙報",L"汇整", L"彙整",L"汇算", L"彙算",L"汇编", L"彙編",L"汇纂", L"彙纂",L"汇辑", L"彙輯",L"汇集", L"彙集",L"形单影只", L"形單影隻",L"形影相吊", L"形影相弔",L"形于", L"形於",L"仿佛", L"彷彿",L"役于", L"役於",L"彼此克制", L"彼此剋制",L"往后", L"往後",L"往日無仇", L"往日無讎",L"往里", L"往裡",L"往复", L"往複",L"很干", L"很乾",L"很凶", L"很兇",L"很丑", L"很醜",L"律历志", L"律曆志",L"后上", L"後上",L"后下", L"後下",L"后世", L"後世",L"后主", L"後主",L"后事", L"後事",L"后人", L"後人",L"后代", L"後代",L"后仰", L"後仰",L"后件", L"後件",L"后任", L"後任",L"后作", L"後作",L"后来", L"後來",L"后偏", L"後偏",L"后备", L"後備",L"后传", L"後傳",L"后分", L"後分",L"后到", L"後到",L"后力不继", L"後力不繼",L"后劲", L"後勁",L"后勤", L"後勤",L"后区", L"後區",L"后半", L"後半",L"后印", L"後印",L"后厝路", L"後厝路",L"后去", L"後去",L"后台", L"後台",L"后台老板", L"後台老板",L"后向", L"後向",L"后周", L"後周",L"后唐", L"後唐",L"后嗣", L"後嗣",L"后园", L"後園",L"后图", L"後圖",L"后土", L"後土",L"后埔", L"後埔",L"后堂", L"後堂",L"后尘", L"後塵",L"后壁", L"後壁",L"后天", L"後天",L"后夫", L"後夫",L"后奏", L"後奏",L"后妻", L"後妻",L"后娘", L"後娘",L"后妇", L"後婦",L"后学", L"後學",L"后宫", L"後宮",L"后山", L"後山",L"后巷", L"後巷",L"后市", L"後市",L"后年", L"後年",L"后几", L"後幾",L"后庄", L"後庄",L"后序", L"後序",L"后座", L"後座",L"后庭", L"後庭",L"后悔", L"後悔",L"后患", L"後患",L"后房", L"後房",L"后手", L"後手",L"后排", L"後排",L"后掠角", L"後掠角",L"后接", L"後接",L"后援", L"後援",L"后撤", L"後撤",L"后攻", L"後攻",L"后放", L"後放",L"后效", L"後效",L"后文", L"後文",L"后方", L"後方",L"后于", L"後於",L"后日", L"後日",L"后昌路", L"後昌路",L"后晋", L"後晉",L"后晌", L"後晌",L"后晚", L"後晚",L"后景", L"後景",L"后会", L"後會",L"后有", L"後有",L"后望镜", L"後望鏡",L"后期", L"後期",L"后果", L"後果",L"后桅", L"後桅",L"后梁", L"後梁",L"后桥", L"後橋",L"后步", L"後步",L"后段", L"後段",L"后殿", L"後殿",L"后母", L"後母",L"后派", L"後派",L"后浪", L"後浪",L"后凉", L"後涼",L"后港", L"後港",L"后汉", L"後漢",L"后为", L"後為",L"后无来者", L"後無來者",L"后照镜", L"後照鏡",L"后燕", L"後燕",L"后父", L"後父",L"后现代", L"後現代",L"后生", L"後生",L"后用", L"後用",L"后由", L"後由",L"后盾", L"後盾",L"后知", L"後知",L"后知后觉", L"後知後覺",L"后福", L"後福",L"后秃", L"後禿",L"后秦", L"後秦",L"后空翻", L"後空翻",L"后窗", L"後窗",L"后站", L"後站",L"后端", L"後端",L"后竹围", L"後竹圍",L"后节", L"後節",L"后篇", L"後篇",L"后缀", L"後綴",L"后继", L"後繼",L"后续", L"後續",L"后置", L"後置",L"后者", L"後者",L"后肢", L"後肢",L"后背", L"後背",L"后脑", L"後腦",L"后脚", L"後腳",L"后腿", L"後腿",L"后膛", L"後膛",L"后花园", L"後花園",L"后菜园", L"後菜園",L"后叶", L"後葉",L"后行", L"後行",L"后街", L"後街",L"后卫", L"後衛",L"后裔", L"後裔",L"后补", L"後補",L"后䙓", L"後襬",L"后视镜", L"後視鏡",L"后言", L"後言",L"后计", L"後計",L"后记", L"後記",L"后设", L"後設",L"后读", L"後讀",L"后走", L"後走",L"后起", L"後起",L"后赵", L"後趙",L"后足", L"後足",L"后跟", L"後跟",L"后路", L"後路",L"后身", L"後身",L"后车", L"後車",L"后辈", L"後輩",L"后轮", L"後輪",L"后转", L"後轉",L"后述", L"後述",L"后退", L"後退",L"后送", L"後送",L"后进", L"後進",L"后过", L"後過",L"后遗症", L"後遺症",L"后边", L"後邊",L"后部", L"後部",L"后镜", L"後鏡",L"后门", L"後門",L"后防", L"後防",L"后院", L"後院",L"后集", L"後集",L"后面", L"後面",L"后面店", L"後面店",L"后项", L"後項",L"后头", L"後頭",L"后颈", L"後頸",L"后顾", L"後顧",L"后魏", L"後魏",L"后点", L"後點",L"后龙", L"後龍",L"徐干", L"徐幹",L"徒托空言", L"徒託空言",L"得克制", L"得剋制",L"从于", L"從於",L"从里到外", L"從裡到外",L"从里向外", L"從裡向外",L"复始", L"復始",L"征人", L"徵人",L"征令", L"徵令",L"征占", L"徵佔",L"征信", L"徵信",L"征候", L"徵候",L"征兆", L"徵兆",L"征兵", L"徵兵",L"征到", L"徵到",L"征募", L"徵募",L"征友", L"徵友",L"征召", L"徵召",L"征名责实", L"徵名責實",L"征吏", L"徵吏",L"征咎", L"徵咎",L"征启", L"徵啟",L"征士", L"徵士",L"征婚", L"徵婚",L"征实", L"徵實",L"征庸", L"徵庸",L"征引", L"徵引",L"征得", L"徵得",L"征怪", L"徵怪",L"征才", L"徵才",L"征招", L"徵招",L"征收", L"徵收",L"征效", L"徵效",L"征文", L"徵文",L"征求", L"徵求",L"征状", L"徵狀",L"征用", L"徵用",L"征发", L"徵發",L"征税", L"徵稅",L"征稿", L"徵稿",L"征答", L"徵答",L"征结", L"徵結",L"征圣", L"徵聖",L"征聘", L"徵聘",L"征训", L"徵訓",L"征询", L"徵詢",L"征调", L"徵調",L"征象", L"徵象",L"征购", L"徵購",L"征迹", L"徵跡",L"征车", L"徵車",L"征辟", L"徵辟",L"征逐", L"徵逐",L"征选", L"徵選",L"征集", L"徵集",L"征风召雨", L"徵風召雨",L"征验", L"徵驗",L"德占", L"德佔",L"心愿", L"心愿",L"心于", L"心於",L"心理", L"心理",L"心细如发", L"心細如髮",L"心系一", L"心繫一",L"心系世", L"心繫世",L"心系中", L"心繫中",L"心系乔", L"心繫乔",L"心系五", L"心繫五",L"心系京", L"心繫京",L"心系人", L"心繫人",L"心系他", L"心繫他",L"心系伊", L"心繫伊",L"心系何", L"心繫何",L"心系你", L"心繫你",L"心系健", L"心繫健",L"心系传", L"心繫傳",L"心系全", L"心繫全",L"心系两", L"心繫兩",L"心系农", L"心繫农",L"心系功", L"心繫功",L"心系动", L"心繫動",L"心系募", L"心繫募",L"心系北", L"心繫北",L"心系十", L"心繫十",L"心系千", L"心繫千",L"心系南", L"心繫南",L"心系台", L"心繫台",L"心系和", L"心繫和",L"心系哪", L"心繫哪",L"心系唐", L"心繫唐",L"心系嘱", L"心繫囑",L"心系四", L"心繫四",L"心系困", L"心繫困",L"心系国", L"心繫國",L"心系在", L"心繫在",L"心系地", L"心繫地",L"心系大", L"心繫大",L"心系天", L"心繫天",L"心系夫", L"心繫夫",L"心系奥", L"心繫奧",L"心系女", L"心繫女",L"心系她", L"心繫她",L"心系妻", L"心繫妻",L"心系妇", L"心繫婦",L"心系子", L"心繫子",L"心系它", L"心繫它",L"心系宣", L"心繫宣",L"心系家", L"心繫家",L"心系富", L"心繫富",L"心系小", L"心繫小",L"心系山", L"心繫山",L"心系川", L"心繫川",L"心系幼", L"心繫幼",L"心系广", L"心繫廣",L"心系彼", L"心繫彼",L"心系德", L"心繫德",L"心系您", L"心繫您",L"心系慈", L"心繫慈",L"心系我", L"心繫我",L"心系摩", L"心繫摩",L"心系故", L"心繫故",L"心系新", L"心繫新",L"心系日", L"心繫日",L"心系昌", L"心繫昌",L"心系晓", L"心繫曉",L"心系曼", L"心繫曼",L"心系东", L"心繫東",L"心系林", L"心繫林",L"心系母", L"心繫母",L"心系民", L"心繫民",L"心系江", L"心繫江",L"心系汶", L"心繫汶",L"心系沈", L"心繫沈",L"心系沙", L"心繫沙",L"心系泰", L"心繫泰",L"心系浙", L"心繫浙",L"心系港", L"心繫港",L"心系湖", L"心繫湖",L"心系澳", L"心繫澳",L"心系灾", L"心繫災",L"心系父", L"心繫父",L"心系生", L"心繫生",L"心系病", L"心繫病",L"心系百", L"心繫百",L"心系的", L"心繫的",L"心系众", L"心繫眾",L"心系社", L"心繫社",L"心系祖", L"心繫祖",L"心系神", L"心繫神",L"心系红", L"心繫紅",L"心系美", L"心繫美",L"心系群", L"心繫群",L"心系老", L"心繫老",L"心系舞", L"心繫舞",L"心系英", L"心繫英",L"心系茶", L"心繫茶",L"心系万", L"心繫萬",L"心系着", L"心繫著",L"心系兰", L"心繫蘭",L"心系西", L"心繫西",L"心系贫", L"心繫貧",L"心系输", L"心繫輸",L"心系近", L"心繫近",L"心系远", L"心繫遠",L"心系选", L"心繫選",L"心系重", L"心繫重",L"心系长", L"心繫長",L"心系阮", L"心繫阮",L"心系震", L"心繫震",L"心系非", L"心繫非",L"心系风", L"心繫風",L"心系香", L"心繫香",L"心系高", L"心繫高",L"心系麦", L"心繫麥",L"心系黄", L"心繫黃",L"心脏", L"心臟",L"心荡", L"心蕩",L"心药", L"心藥",L"心里面", L"心裏面",L"心里", L"心裡",L"心长发短", L"心長髮短",L"心余", L"心餘",L"必须", L"必須",L"忙并", L"忙併",L"忙里", L"忙裡",L"忙里偷闲", L"忙裡偷閒",L"忠人之托", L"忠人之托",L"忠仆", L"忠僕",L"忠于", L"忠於",L"快干", L"快乾",L"快克制", L"快剋制",L"快快当当", L"快快當當",L"快冲", L"快衝",L"忽前忽后", L"忽前忽後",L"怎么", L"怎麼",L"怎么着", L"怎麼著",L"怒于", L"怒於",L"怒发冲冠", L"怒髮衝冠",L"思前思后", L"思前思後",L"思前想后", L"思前想後",L"思如泉涌", L"思如泉湧",L"怠于", L"怠於",L"急于", L"急於",L"急冲而下", L"急衝而下",L"性征", L"性徵",L"性欲", L"性慾",L"怪里怪气", L"怪裡怪氣",L"怫郁", L"怫鬱",L"恂栗", L"恂慄",L"恒生指数", L"恒生指數",L"恒生股价指数", L"恒生股價指數",L"恒生银行", L"恒生銀行",L"恕乏价催", L"恕乏价催",L"息交绝游", L"息交絕遊",L"息谷", L"息穀",L"恰才", L"恰纔",L"悍药", L"悍藥",L"悒郁", L"悒鬱",L"悠悠荡荡", L"悠悠蕩蕩",L"悠荡", L"悠蕩",L"悠游", L"悠遊",L"您克制", L"您剋制",L"悲筑", L"悲筑",L"悲郁", L"悲鬱",L"闷着头儿干", L"悶著頭兒幹",L"悸栗", L"悸慄",L"情欲", L"情慾",L"惇朴", L"惇樸",L"恶直丑正", L"惡直醜正",L"恶斗", L"惡鬥",L"想克制", L"想剋制",L"惴栗", L"惴慄",L"意占", L"意佔",L"意克制", L"意剋制",L"意大利面", L"意大利麵",L"意面", L"意麵",L"爱困", L"愛睏",L"感冒药", L"感冒藥",L"感于", L"感於",L"愿朴", L"愿樸",L"愿而恭", L"愿而恭",L"栗冽", L"慄冽",L"栗栗", L"慄慄",L"慌里慌张", L"慌裡慌張",L"庆吊", L"慶弔",L"庆历", L"慶曆",L"庆历史", L"慶歷史",L"欲令智昏", L"慾令智昏",L"欲壑难填", L"慾壑難填",L"欲念", L"慾念",L"欲望", L"慾望",L"欲海", L"慾海",L"欲火", L"慾火",L"欲障", L"慾障",L"忧郁", L"憂鬱",L"凭几", L"憑几",L"凭吊", L"憑弔",L"凭折", L"憑摺",L"凭准", L"憑準",L"凭借", L"憑藉",L"凭借着", L"憑藉著",L"恳托", L"懇託",L"懈松", L"懈鬆",L"应克制", L"應剋制",L"应征", L"應徵",L"应钟", L"應鐘",L"懔栗", L"懍慄",L"蒙懂", L"懞懂",L"蒙蒙懂懂", L"懞懞懂懂",L"蒙直", L"懞直",L"惩前毖后", L"懲前毖後",L"惩忿窒欲", L"懲忿窒欲",L"怀里", L"懷裡",L"怀表", L"懷錶",L"怀钟", L"懷鐘",L"悬梁", L"懸樑",L"悬臂梁", L"懸臂樑",L"悬钟", L"懸鐘",L"懿范", L"懿範",L"恋恋不舍", L"戀戀不捨",L"成于", L"成於",L"成于思", L"成於思",L"成药", L"成藥",L"我克制", L"我剋制",L"戬谷", L"戩穀",L"截发", L"截髮",L"战天斗地", L"戰天鬥地",L"战后", L"戰後",L"战栗", L"戰慄",L"战斗", L"戰鬥",L"戏彩娱亲", L"戲綵娛親",L"戴表", L"戴錶",L"戴发含齿", L"戴髮含齒",L"房里", L"房裡",L"所云", L"所云",L"所云云", L"所云云",L"所占", L"所佔",L"所占卜", L"所占卜",L"所占星", L"所占星",L"所占算", L"所占算",L"所托", L"所託",L"扁拟谷盗虫", L"扁擬穀盜蟲",L"手塚治虫", L"手塚治虫",L"手冢治虫", L"手塚治虫",L"手折", L"手摺",L"手表态", L"手表態",L"手表明", L"手表明",L"手表决", L"手表決",L"手表演", L"手表演",L"手表现", L"手表現",L"手表示", L"手表示",L"手表达", L"手表達",L"手表露", L"手表露",L"手表面", L"手表面",L"手里", L"手裡",L"手表", L"手錶",L"手松", L"手鬆",L"才克制", L"才剋制",L"才干休", L"才干休",L"才干戈", L"才干戈",L"才干扰", L"才干擾",L"才干政", L"才干政",L"才干涉", L"才干涉",L"才干预", L"才干預",L"才干", L"才幹",L"扎好底子", L"扎好底子",L"扎好根", L"扎好根",L"扑作教刑", L"扑作教刑",L"扑打", L"扑打",L"扑挞", L"扑撻",L"打干哕", L"打乾噦",L"打并", L"打併",L"打出吊入", L"打出弔入",L"打卡钟", L"打卡鐘",L"打吨", L"打吨",L"打干", L"打幹",L"打拼", L"打拚",L"打断发", L"打斷發",L"打谷", L"打穀",L"打着钟", L"打著鐘",L"打路庄板", L"打路莊板",L"打钟", L"打鐘",L"打斗", L"打鬥",L"托管国", L"托管國",L"扛大梁", L"扛大樑",L"扞御", L"扞禦",L"扯面", L"扯麵",L"扶余国", L"扶餘國",L"批准的", L"批准的",L"批复", L"批複",L"批注", L"批註",L"批斗", L"批鬥",L"承先启后", L"承先啟後",L"承前启后", L"承前啟後",L"承制", L"承製",L"抑制作用", L"抑制作用",L"抑郁", L"抑鬱",L"抓奸", L"抓姦",L"抓药", L"抓藥",L"抓斗", L"抓鬥",L"投药", L"投藥",L"抗癌药", L"抗癌藥",L"抗御", L"抗禦",L"抗药", L"抗藥",L"折向往", L"折向往",L"折子戏", L"折子戲",L"折戟沈河", L"折戟沈河",L"折冲", L"折衝",L"披榛采兰", L"披榛採蘭",L"披头散发", L"披頭散髮",L"披发", L"披髮",L"抱朴而长吟兮", L"抱朴而長吟兮",L"抱素怀朴", L"抱素懷樸",L"抵御", L"抵禦",L"抹干", L"抹乾",L"抽公签", L"抽公籤",L"抽签", L"抽籤",L"抿发", L"抿髮",L"拂钟无声", L"拂鐘無聲",L"拆伙", L"拆夥",L"拈须", L"拈鬚",L"拉克施尔德钟", L"拉克施爾德鐘",L"拉杆", L"拉杆",L"拉纤", L"拉縴",L"拉面上", L"拉面上",L"拉面具", L"拉面具",L"拉面前", L"拉面前",L"拉面巾", L"拉面巾",L"拉面无", L"拉面無",L"拉面皮", L"拉面皮",L"拉面罩", L"拉面罩",L"拉面色", L"拉面色",L"拉面部", L"拉面部",L"拉面", L"拉麵",L"拒人于", L"拒人於",L"拒于", L"拒於",L"拓朴", L"拓樸",L"拔发", L"拔髮",L"拔须", L"拔鬚",L"拗别", L"拗彆",L"拘于", L"拘於",L"拙于", L"拙於",L"拙朴", L"拙樸",L"拼却", L"拚卻",L"拼命", L"拚命",L"拼舍", L"拚捨",L"拼死", L"拚死",L"拼生尽死", L"拚生盡死",L"拼绝", L"拚絕",L"拼老命", L"拚老命",L"拼斗", L"拚鬥",L"拜托", L"拜託",L"括发", L"括髮",L"拭干", L"拭乾",L"拮据", L"拮据",L"拼死拼活", L"拼死拼活",L"拾沈", L"拾瀋",L"拿下表", L"拿下錶",L"拿下钟", L"拿下鐘",L"拿准", L"拿準",L"拿破仑", L"拿破崙",L"挂名", L"挂名",L"挂图", L"挂圖",L"挂帅", L"挂帥",L"挂彩", L"挂彩",L"挂念", L"挂念",L"挂号", L"挂號",L"挂车", L"挂車",L"挂面", L"挂面",L"指手划脚", L"指手劃腳",L"挌斗", L"挌鬥",L"挑大梁", L"挑大樑",L"挑斗", L"挑鬥",L"振荡", L"振蕩",L"捆扎", L"捆紮",L"捉奸徒", L"捉奸徒",L"捉奸细", L"捉奸細",L"捉奸贼", L"捉奸賊",L"捉奸党", L"捉奸黨",L"捉奸", L"捉姦",L"捉发", L"捉髮",L"捍御", L"捍禦",L"捏面人", L"捏麵人",L"舍不得", L"捨不得",L"舍出", L"捨出",L"舍去", L"捨去",L"舍命", L"捨命",L"舍堕", L"捨墮",L"舍安就危", L"捨安就危",L"舍实", L"捨實",L"舍己从人", L"捨己從人",L"舍己救人", L"捨己救人",L"舍己为人", L"捨己為人",L"舍己为公", L"捨己為公",L"舍己为国", L"捨己為國",L"舍得", L"捨得",L"舍我其谁", L"捨我其誰",L"舍本逐末", L"捨本逐末",L"舍弃", L"捨棄",L"舍死忘生", L"捨死忘生",L"舍生", L"捨生",L"舍短取长", L"捨短取長",L"舍身", L"捨身",L"舍车保帅", L"捨車保帥",L"舍近求远", L"捨近求遠",L"卷住", L"捲住",L"卷来", L"捲來",L"卷儿", L"捲兒",L"卷入", L"捲入",L"卷动", L"捲動",L"卷去", L"捲去",L"卷图", L"捲圖",L"卷土重来", L"捲土重來",L"卷尺", L"捲尺",L"卷心菜", L"捲心菜",L"卷成", L"捲成",L"卷曲", L"捲曲",L"卷款", L"捲款",L"卷毛", L"捲毛",L"卷烟", L"捲煙",L"卷筒", L"捲筒",L"卷帘", L"捲簾",L"卷纸", L"捲紙",L"卷缩", L"捲縮",L"卷舌", L"捲舌",L"卷舖盖", L"捲舖蓋",L"卷菸", L"捲菸",L"卷袖", L"捲袖",L"卷走", L"捲走",L"卷起", L"捲起",L"卷轴", L"捲軸",L"卷逃", L"捲逃",L"卷铺盖", L"捲鋪蓋",L"卷云", L"捲雲",L"卷风", L"捲風",L"卷发", L"捲髮",L"捵面", L"捵麵",L"捶炼", L"捶鍊",L"扫荡", L"掃蕩",L"掌柜", L"掌柜",L"排骨面", L"排骨麵",L"挂帘", L"掛帘",L"挂历", L"掛曆",L"挂钩", L"掛鈎",L"挂钟", L"掛鐘",L"采下", L"採下",L"采伐", L"採伐",L"采住", L"採住",L"采信", L"採信",L"采光", L"採光",L"采到", L"採到",L"采制", L"採制",L"采区", L"採區",L"采去", L"採去",L"采取", L"採取",L"采回", L"採回",L"采在", L"採在",L"采好", L"採好",L"采得", L"採得",L"采拾", L"採拾",L"采挖", L"採挖",L"采掘", L"採掘",L"采摘", L"採摘",L"采摭", L"採摭",L"采择", L"採擇",L"采撷", L"採擷",L"采收", L"採收",L"采料", L"採料",L"采暖", L"採暖",L"采桑", L"採桑",L"采样", L"採樣",L"采樵人", L"採樵人",L"采树种", L"採樹種",L"采气", L"採氣",L"采油", L"採油",L"采为", L"採為",L"采煤", L"採煤",L"采获", L"採獲",L"采猎", L"採獵",L"采珠", L"採珠",L"采生折割", L"採生折割",L"采用", L"採用",L"采的", L"採的",L"采石", L"採石",L"采砂场", L"採砂場",L"采矿", L"採礦",L"采种", L"採種",L"采空区", L"採空區",L"采空采穗", L"採空採穗",L"采納", L"採納",L"采纳", L"採納",L"采给", L"採給",L"采花", L"採花",L"采芹人", L"採芹人",L"采茶", L"採茶",L"采菊", L"採菊",L"采莲", L"採蓮",L"采薇", L"採薇",L"采薪", L"採薪",L"采药", L"採藥",L"采行", L"採行",L"采补", L"採補",L"采访", L"採訪",L"采证", L"採證",L"采买", L"採買",L"采购", L"採購",L"采办", L"採辦",L"采运", L"採運",L"采过", L"採過",L"采选", L"採選",L"采金", L"採金",L"采录", L"採錄",L"采铁", L"採鐵",L"采集", L"採集",L"采风", L"採風",L"采风问俗", L"採風問俗",L"采食", L"採食",L"采盐", L"採鹽",L"掣签", L"掣籤",L"接着说", L"接著說",L"控制", L"控制",L"推情准理", L"推情準理",L"推托之词", L"推托之詞",L"推舟于陆", L"推舟於陸",L"推托", L"推託",L"提子干", L"提子乾",L"提心吊胆", L"提心弔膽",L"提摩太后书", L"提摩太後書",L"插于", L"插於",L"换签", L"換籤",L"换药", L"換藥",L"换只", L"換隻",L"换发", L"換髮",L"握发", L"握髮",L"揩干", L"揩乾",L"揪采", L"揪採",L"揪发", L"揪髮",L"揪须", L"揪鬚",L"揭丑", L"揭醜",L"挥手表", L"揮手表",L"挥杆", L"揮杆",L"搋面", L"搋麵",L"损于", L"損於",L"搏斗", L"搏鬥",L"摇摇荡荡", L"搖搖蕩蕩",L"摇荡", L"搖蕩",L"捣鬼吊白", L"搗鬼弔白",L"搤肮拊背", L"搤肮拊背",L"搬斗", L"搬鬥",L"搭干铺", L"搭乾鋪",L"搭伙", L"搭夥",L"抢占", L"搶佔",L"搽药", L"搽藥",L"摧坚获丑", L"摧堅獲醜",L"摭采", L"摭採",L"摸棱", L"摸稜",L"摸钟", L"摸鐘",L"折合", L"摺合",L"折奏", L"摺奏",L"折子", L"摺子",L"折尺", L"摺尺",L"折扇", L"摺扇",L"折梯", L"摺梯",L"折椅", L"摺椅",L"折叠", L"摺疊",L"折痕", L"摺痕",L"折篷", L"摺篷",L"折纸", L"摺紙",L"折裙", L"摺裙",L"撇吊", L"撇弔",L"捞干", L"撈乾",L"捞面", L"撈麵",L"撚须", L"撚鬚",L"撞球台", L"撞球檯",L"撞钟", L"撞鐘",L"撞阵冲军", L"撞陣衝軍",L"撤并", L"撤併",L"撤后", L"撤後",L"拨谷", L"撥穀",L"撩斗", L"撩鬥",L"播于", L"播於",L"扑冬", L"撲鼕",L"扑冬冬", L"撲鼕鼕",L"擀面", L"擀麵",L"击扑", L"擊扑",L"击钟", L"擊鐘",L"操作钟", L"操作鐘",L"担仔面", L"擔仔麵",L"担担面", L"擔擔麵",L"担着", L"擔著",L"担负着", L"擔負著",L"擘划", L"擘劃",L"据云", L"據云",L"据干而窥井底", L"據榦而窺井底",L"擢发", L"擢髮",L"擦干", L"擦乾",L"擦干净", L"擦乾淨",L"擦药", L"擦藥",L"拧干", L"擰乾",L"摆钟", L"擺鐘",L"摄制", L"攝製",L"支干", L"支幹",L"支杆", L"支杆",L"收获", L"收穫",L"改征", L"改徵",L"攻占", L"攻佔",L"放蒙挣", L"放懞掙",L"放荡", L"放蕩",L"放松", L"放鬆",L"故事里", L"故事裡",L"故云", L"故云",L"敏于", L"敏於",L"救药", L"救藥",L"败于", L"敗於",L"叙说着", L"敘說著",L"教学钟", L"教學鐘",L"教于", L"教於",L"教范", L"教範",L"敢干", L"敢幹",L"敢情欲", L"敢情欲",L"敢斗了胆", L"敢斗了膽",L"散伙", L"散夥",L"散于", L"散於",L"散荡", L"散蕩",L"敦朴", L"敦樸",L"敬挽", L"敬輓",L"敲扑", L"敲扑",L"敲钟", L"敲鐘",L"整庄", L"整莊",L"整只", L"整隻",L"整发用品", L"整髮用品",L"敌后", L"敵後",L"敌忾同仇", L"敵愾同讎",L"敷药", L"敷藥",L"数天后", L"數天後",L"数字表", L"數字錶",L"数字钟", L"數字鐘",L"数字钟表", L"數字鐘錶",L"数罪并罚", L"數罪併罰",L"数与虏确", L"數與虜确",L"文丑", L"文丑",L"文汇报", L"文匯報",L"文后", L"文後",L"文征明", L"文徵明",L"文思泉涌", L"文思泉湧",L"文采郁郁", L"文采郁郁",L"斗转参横", L"斗轉參橫",L"斫雕为朴", L"斫雕為樸",L"新历", L"新曆",L"新历史", L"新歷史",L"新扎", L"新紮",L"新庄", L"新莊",L"新庄市", L"新莊市",L"斲雕为朴", L"斲雕為樸",L"断后", L"斷後",L"断发", L"斷髮",L"断发文身", L"斷髮文身",L"方便面", L"方便麵",L"方几", L"方几",L"方向往", L"方向往",L"方志", L"方誌",L"方面", L"方面",L"于0", L"於0",L"于1", L"於1",L"于2", L"於2",L"于3", L"於3",L"于4", L"於4",L"于5", L"於5",L"于6", L"於6",L"于7", L"於7",L"于8", L"於8",L"于9", L"於9",L"于一", L"於一",L"于一役", L"於一役",L"于七", L"於七",L"于三", L"於三",L"于世", L"於世",L"于之", L"於之",L"于乎", L"於乎",L"于九", L"於九",L"于事", L"於事",L"于二", L"於二",L"于五", L"於五",L"于人", L"於人",L"于今", L"於今",L"于他", L"於他",L"于伏", L"於伏",L"于何", L"於何",L"于你", L"於你",L"于八", L"於八",L"于六", L"於六",L"于克制", L"於剋制",L"于前", L"於前",L"于劣", L"於劣",L"于勤", L"於勤",L"于十", L"於十",L"于半", L"於半",L"于呼哀哉", L"於呼哀哉",L"于四", L"於四",L"于国", L"於國",L"于坏", L"於坏",L"于垂", L"於垂",L"于夫罗", L"於夫羅",L"於夫罗", L"於夫羅",L"於夫羅", L"於夫羅",L"于她", L"於她",L"于好", L"於好",L"于始", L"於始",L"於姓", L"於姓",L"于它", L"於它",L"于家", L"於家",L"于密", L"於密",L"于差", L"於差",L"于己", L"於己",L"于市", L"於市",L"于幕", L"於幕",L"于弱", L"於弱",L"于强", L"於強",L"于后", L"於後",L"于征", L"於徵",L"于心", L"於心",L"于怀", L"於懷",L"于我", L"於我",L"于戏", L"於戲",L"于敝", L"於敝",L"于斯", L"於斯",L"于是", L"於是",L"于是乎", L"於是乎",L"于时", L"於時",L"于梨华", L"於梨華",L"於梨華", L"於梨華",L"于乐", L"於樂",L"于此", L"於此",L"於氏", L"於氏",L"于民", L"於民",L"于水", L"於水",L"于法", L"於法",L"于潜县", L"於潛縣",L"于火", L"於火",L"于焉", L"於焉",L"于墙", L"於牆",L"于物", L"於物",L"于毕", L"於畢",L"于尽", L"於盡",L"于盲", L"於盲",L"于祂", L"於祂",L"于穆", L"於穆",L"于终", L"於終",L"于美", L"於美",L"于色", L"於色",L"于菟", L"於菟",L"于蓝", L"於藍",L"于行", L"於行",L"于衷", L"於衷",L"于该", L"於該",L"于农", L"於農",L"于途", L"於途",L"于过", L"於過",L"于邑", L"於邑",L"于丑", L"於醜",L"于野", L"於野",L"于陆", L"於陸",L"于０", L"於０",L"于１", L"於１",L"于２", L"於２",L"于３", L"於３",L"于４", L"於４",L"于５", L"於５",L"于６", L"於６",L"于７", L"於７",L"于８", L"於８",L"于９", L"於９",L"施舍", L"施捨",L"施于", L"施於",L"施舍之道", L"施舍之道",L"施药", L"施藥",L"旁征博引", L"旁徵博引",L"旁注", L"旁註",L"旅游", L"旅遊",L"旋干转坤", L"旋乾轉坤",L"旋绕着", L"旋繞著",L"旋回", L"旋迴",L"族里", L"族裡",L"旗杆", L"旗杆",L"日占", L"日佔",L"日子里", L"日子裡",L"日后", L"日後",L"日晒", L"日晒",L"日历", L"日曆",L"日历史", L"日歷史",L"日志", L"日誌",L"早于", L"早於",L"旱干", L"旱乾",L"昆仑山", L"昆崙山",L"升平", L"昇平",L"升阳", L"昇陽",L"昊天不吊", L"昊天不弔",L"明征", L"明徵",L"明目张胆", L"明目張胆",L"明窗净几", L"明窗淨几",L"明范", L"明範",L"明里", L"明裡",L"易克制", L"易剋制",L"易于", L"易於",L"星巴克", L"星巴克",L"星历", L"星曆",L"星期后", L"星期後",L"星历史", L"星歷史",L"星辰表", L"星辰錶",L"春假里", L"春假裡",L"春天里", L"春天裡",L"春日里", L"春日裡",L"春药", L"春藥",L"春游", L"春遊",L"春香斗学", L"春香鬥學",L"时钟", L"時鐘",L"时间里", L"時間裡",L"晃荡", L"晃蕩",L"晋升", L"晉陞",L"晒干", L"晒乾",L"晒伤", L"晒傷",L"晒图", L"晒圖",L"晒图纸", L"晒圖紙",L"晒成", L"晒成",L"晒晒", L"晒晒",L"晒烟", L"晒煙",L"晒种", L"晒種",L"晒衣", L"晒衣",L"晒黑", L"晒黑",L"晚于", L"晚於",L"晚钟", L"晚鐘",L"晞发", L"晞髮",L"晨钟", L"晨鐘",L"普冬冬", L"普鼕鼕",L"景致", L"景緻",L"晾干", L"晾乾",L"晕船药", L"暈船藥",L"晕车药", L"暈車藥",L"暑假里", L"暑假裡",L"暗地里", L"暗地裡",L"暗沟里", L"暗溝裡",L"暗里", L"暗裡",L"暗斗", L"暗鬥",L"畅游", L"暢遊",L"暴敛横征", L"暴斂橫徵",L"暴晒", L"暴晒",L"历元", L"曆元",L"历命", L"曆命",L"历始", L"曆始",L"历室", L"曆室",L"历尾", L"曆尾",L"历数", L"曆數",L"历日", L"曆日",L"历书", L"曆書",L"历本", L"曆本",L"历法", L"曆法",L"历纪", L"曆紀",L"历象", L"曆象",L"曝晒", L"曝晒",L"晒谷", L"曬穀",L"曰云", L"曰云",L"更仆难数", L"更僕難數",L"更签", L"更籤",L"更钟", L"更鐘",L"书后", L"書後",L"书呆子", L"書獃子",L"书签", L"書籤",L"曼谷人", L"曼谷人",L"曾朴", L"曾樸",L"最多", L"最多",L"最后", L"最後",L"会上签署", L"會上簽署",L"会上签订", L"會上簽訂",L"会占", L"會佔",L"会占卜", L"會占卜",L"会干", L"會幹",L"会吊", L"會弔",L"会后", L"會後",L"会里", L"會裡",L"月后", L"月後",L"月历", L"月曆",L"月历史", L"月歷史",L"月离于毕", L"月離於畢",L"月面", L"月面",L"月丽于箕", L"月麗於箕",L"有事之无范", L"有事之無範",L"有仆", L"有僕",L"有够赞", L"有夠讚",L"有征伐", L"有征伐",L"有征战", L"有征戰",L"有征服", L"有征服",L"有征讨", L"有征討",L"有征", L"有徵",L"有恒街", L"有恒街",L"有栖川", L"有栖川",L"有准", L"有準",L"有棱有角", L"有稜有角",L"有只", L"有隻",L"有余", L"有餘",L"有发头陀寺", L"有髮頭陀寺",L"服于", L"服於",L"服药", L"服藥",L"望了望", L"望了望",L"望着表", L"望著錶",L"望着钟", L"望著鐘",L"望着钟表", L"望著鐘錶",L"朝乾夕惕", L"朝乾夕惕",L"朝后", L"朝後",L"朝钟", L"朝鐘",L"朦胧", L"朦朧",L"蒙胧", L"朦朧",L"木偶戏扎", L"木偶戲紮",L"木杆", L"木杆",L"木材干馏", L"木材乾餾",L"木梁", L"木樑",L"木制", L"木製",L"木钟", L"木鐘",L"未干", L"未乾",L"末药", L"末藥",L"本征", L"本徵",L"术赤", L"朮赤",L"朱仑街", L"朱崙街",L"朱庆余", L"朱慶餘",L"朱理安历", L"朱理安曆",L"朱理安历史", L"朱理安歷史",L"杆子", L"杆子",L"李連杰", L"李連杰",L"李连杰", L"李連杰",L"材干", L"材幹",L"村庄", L"村莊",L"村落发", L"村落發",L"村里", L"村裡",L"杜老志道", L"杜老誌道",L"杞宋无征", L"杞宋無徵",L"束发", L"束髮",L"杯干", L"杯乾",L"杯面", L"杯麵",L"杰特", L"杰特",L"东周钟", L"東周鐘",L"东岳", L"東嶽",L"东冲西突", L"東衝西突",L"东游", L"東遊",L"松山庄", L"松山庄",L"松柏后凋", L"松柏後凋",L"板着脸", L"板著臉",L"板荡", L"板蕩",L"林宏岳", L"林宏嶽",L"林郁方", L"林郁方",L"林钟", L"林鐘",L"果干", L"果乾",L"果子干", L"果子乾",L"枝不得大于干", L"枝不得大於榦",L"枝干", L"枝幹",L"枯干", L"枯乾",L"台历", L"枱曆",L"架钟", L"架鐘",L"某只", L"某隻",L"染指于", L"染指於",L"染发", L"染髮",L"柜上", L"柜上",L"柜子", L"柜子",L"柜柳", L"柜柳",L"柱梁", L"柱樑",L"柳诒征", L"柳詒徵",L"栖栖皇皇", L"栖栖皇皇",L"校准", L"校準",L"校仇", L"校讎",L"核准的", L"核准的",L"格于", L"格於",L"格范", L"格範",L"格里历", L"格里曆",L"格里高利历", L"格里高利曆",L"格斗", L"格鬥",L"桂圆干", L"桂圓乾",L"桅杆", L"桅杆",L"案发后", L"案發後",L"桌几", L"桌几",L"桌历", L"桌曆",L"桌历史", L"桌歷史",L"桑干", L"桑乾",L"梁上君子", L"梁上君子",L"条干", L"條幹",L"梨干", L"梨乾",L"梯冲", L"梯衝",L"械系", L"械繫",L"械斗", L"械鬥",L"弃舍", L"棄捨",L"棉制", L"棉製",L"棒子面", L"棒子麵",L"枣庄", L"棗莊",L"栋梁", L"棟樑",L"棫朴", L"棫樸",L"森林里", L"森林裡",L"棺材里", L"棺材裡",L"植发", L"植髮",L"椰枣干", L"椰棗乾",L"楚庄问鼎", L"楚莊問鼎",L"楚庄王", L"楚莊王",L"楚庄绝缨", L"楚莊絕纓",L"桢干", L"楨幹",L"业余", L"業餘",L"榨干", L"榨乾",L"荣登后座", L"榮登后座",L"杠杆", L"槓桿",L"乐器钟", L"樂器鐘",L"樊于期", L"樊於期",L"梁上", L"樑上",L"梁柱", L"樑柱",L"标杆", L"標杆",L"标标致致", L"標標致致",L"标准", L"標準",L"标签", L"標籤",L"标致", L"標緻",L"标注", L"標註",L"标志", L"標誌",L"模棱", L"模稜",L"模范", L"模範",L"模范棒棒堂", L"模范棒棒堂",L"模制", L"模製",L"样范", L"樣範",L"樵采", L"樵採",L"朴修斯", L"樸修斯",L"朴厚", L"樸厚",L"朴学", L"樸學",L"朴实", L"樸實",L"朴念仁", L"樸念仁",L"朴拙", L"樸拙",L"朴樕", L"樸樕",L"朴父", L"樸父",L"朴直", L"樸直",L"朴素", L"樸素",L"朴讷", L"樸訥",L"朴质", L"樸質",L"朴鄙", L"樸鄙",L"朴重", L"樸重",L"朴野", L"樸野",L"朴钝", L"樸鈍",L"朴陋", L"樸陋",L"朴马", L"樸馬",L"朴鲁", L"樸魯",L"树干", L"樹榦",L"树梁", L"樹樑",L"桥梁", L"橋樑",L"機械系", L"機械系",L"机械系", L"機械系",L"机械表", L"機械錶",L"机械钟", L"機械鐘",L"机械钟表", L"機械鐘錶",L"机绣", L"機繡",L"横征暴敛", L"橫徵暴斂",L"横杆", L"橫杆",L"横梁", L"橫樑",L"横冲", L"橫衝",L"台子", L"檯子",L"台布", L"檯布",L"台灯", L"檯燈",L"台球", L"檯球",L"台面", L"檯面",L"柜台", L"櫃檯",L"栉发工", L"櫛髮工",L"栏杆", L"欄杆",L"欲海难填", L"欲海難填",L"欺蒙", L"欺矇",L"歇后", L"歇後",L"歌钟", L"歌鐘",L"欧游", L"歐遊",L"止咳药", L"止咳藥",L"止于", L"止於",L"止痛药", L"止痛藥",L"止血药", L"止血藥",L"正在叱咤", L"正在叱咤",L"正官庄", L"正官庄",L"正后", L"正後",L"正当着", L"正當著",L"此后", L"此後",L"武丑", L"武丑",L"武斗", L"武鬥",L"岁聿云暮", L"歲聿云暮",L"历史里", L"歷史裡",L"归并", L"歸併",L"归于", L"歸於",L"归余", L"歸餘",L"歹斗", L"歹鬥",L"死后", L"死後",L"死于", L"死於",L"死胡同", L"死胡同",L"死里求生", L"死裡求生",L"死里逃生", L"死裡逃生",L"殖谷", L"殖穀",L"残肴", L"殘肴",L"残余", L"殘餘",L"僵尸", L"殭屍",L"殷师牛斗", L"殷師牛鬥",L"杀虫药", L"殺蟲藥",L"壳里", L"殼裡",L"殿后", L"殿後",L"殿钟自鸣", L"殿鐘自鳴",L"毁于", L"毀於",L"毁钟为铎", L"毀鐘為鐸",L"殴斗", L"毆鬥",L"母范", L"母範",L"母丑", L"母醜",L"每每只", L"每每只",L"每只", L"每隻",L"毒药", L"毒藥",L"比划", L"比劃",L"毛坏", L"毛坏",L"毛姜", L"毛薑",L"毛发", L"毛髮",L"毫厘", L"毫釐",L"毫发", L"毫髮",L"气冲斗牛", L"氣沖斗牛",L"气郁", L"氣鬱",L"氤郁", L"氤鬱",L"水来汤里去", L"水來湯裡去",L"水准", L"水準",L"水里", L"水裡",L"水里鄉", L"水里鄉",L"水里乡", L"水里鄉",L"永历", L"永曆",L"永历史", L"永歷史",L"永志不忘", L"永誌不忘",L"求知欲", L"求知慾",L"求签", L"求籤",L"求道于盲", L"求道於盲",L"池里", L"池裡",L"污蔑", L"污衊",L"汲于", L"汲於",L"决斗", L"決鬥",L"沈淀", L"沈澱",L"沈着", L"沈著",L"沈郁", L"沈鬱",L"沉淀", L"沉澱",L"沉郁", L"沉鬱",L"没干没净", L"沒乾沒淨",L"没事干", L"沒事幹",L"没干", L"沒幹",L"没折至", L"沒摺至",L"没梢干", L"沒梢幹",L"没样范", L"沒樣範",L"没准", L"沒準",L"没药", L"沒藥",L"冲冠发怒", L"沖冠髮怒",L"沙里淘金", L"沙裡淘金",L"河岳", L"河嶽",L"河流汇集", L"河流匯集",L"河里", L"河裡",L"油斗", L"油鬥",L"油面", L"油麵",L"治愈", L"治癒",L"沿溯", L"沿泝",L"法占", L"法佔",L"法自制", L"法自制",L"泛游", L"泛遊",L"泡制", L"泡製",L"泡面", L"泡麵",L"波棱菜", L"波稜菜",L"波发藻", L"波髮藻",L"泥于", L"泥於",L"注云", L"注云",L"注释", L"注釋",L"泰山梁木", L"泰山梁木",L"泱郁", L"泱鬱",L"泳气钟", L"泳氣鐘",L"洄游", L"洄遊",L"洋面", L"洋麵",L"洒家", L"洒家",L"洒扫", L"洒掃",L"洒水", L"洒水",L"洒洒", L"洒洒",L"洒淅", L"洒淅",L"洒涤", L"洒滌",L"洒濯", L"洒濯",L"洒然", L"洒然",L"洒脱", L"洒脫",L"洗炼", L"洗鍊",L"洗练", L"洗鍊",L"洗发", L"洗髮",L"洛钟东应", L"洛鐘東應",L"泄欲", L"洩慾",L"洪范", L"洪範",L"洪适", L"洪适",L"洪钟", L"洪鐘",L"汹涌", L"洶湧",L"派团参加", L"派團參加",L"流征", L"流徵",L"流于", L"流於",L"流荡", L"流蕩",L"流风余俗", L"流風餘俗",L"流风余韵", L"流風餘韻",L"浩浩荡荡", L"浩浩蕩蕩",L"浩荡", L"浩蕩",L"浪琴表", L"浪琴錶",L"浪荡", L"浪蕩",L"浪游", L"浪遊",L"浮于", L"浮於",L"浮荡", L"浮蕩",L"浮夸", L"浮誇",L"浮松", L"浮鬆",L"海上布雷", L"海上佈雷",L"海干", L"海乾",L"海湾布雷", L"海灣佈雷",L"涂坤", L"涂坤",L"涂壮勋", L"涂壯勳",L"涂壯勳", L"涂壯勳",L"涂天相", L"涂天相",L"涂序瑄", L"涂序瑄",L"涂澤民", L"涂澤民",L"涂泽民", L"涂澤民",L"涂绍煃", L"涂紹煃",L"涂羽卿", L"涂羽卿",L"涂謹申", L"涂謹申",L"涂谨申", L"涂謹申",L"涂逢年", L"涂逢年",L"涂醒哲", L"涂醒哲",L"涂長望", L"涂長望",L"涂长望", L"涂長望",L"涂鸿钦", L"涂鴻欽",L"涂鴻欽", L"涂鴻欽",L"消炎药", L"消炎藥",L"消肿药", L"消腫藥",L"液晶表", L"液晶錶",L"涳蒙", L"涳濛",L"涸干", L"涸乾",L"凉面", L"涼麵",L"淋余土", L"淋餘土",L"淑范", L"淑範",L"泪干", L"淚乾",L"泪如泉涌", L"淚如泉湧",L"淡于", L"淡於",L"淡蒙蒙", L"淡濛濛",L"淡朱", L"淡硃",L"净余", L"淨餘",L"净发", L"淨髮",L"淫欲", L"淫慾",L"淫荡", L"淫蕩",L"淬炼", L"淬鍊",L"深山何处钟", L"深山何處鐘",L"深渊里", L"深淵裡",L"淳于", L"淳于",L"淳朴", L"淳樸",L"渊淳岳峙", L"淵淳嶽峙",L"浅淀", L"淺澱",L"清心寡欲", L"清心寡欲",L"清汤挂面", L"清湯掛麵",L"减肥药", L"減肥藥",L"渠冲", L"渠衝",L"港制", L"港製",L"浑朴", L"渾樸",L"浑个", L"渾箇",L"凑合着", L"湊合著",L"湖里", L"湖裡",L"湘绣", L"湘繡",L"湘累", L"湘纍",L"湟潦生苹", L"湟潦生苹",L"涌上", L"湧上",L"涌来", L"湧來",L"涌入", L"湧入",L"涌出", L"湧出",L"涌向", L"湧向",L"涌泉", L"湧泉",L"涌现", L"湧現",L"涌起", L"湧起",L"涌进", L"湧進",L"湮郁", L"湮鬱",L"汤下面", L"湯下麵",L"汤团", L"湯糰",L"汤药", L"湯藥",L"汤面", L"湯麵",L"源于", L"源於",L"准不准", L"準不準",L"准例", L"準例",L"准保", L"準保",L"准备", L"準備",L"准儿", L"準兒",L"准分子", L"準分子",L"准则", L"準則",L"准噶尔", L"準噶爾",L"准定", L"準定",L"准平原", L"準平原",L"准度", L"準度",L"准式", L"準式",L"准拿督", L"準拿督",L"准据", L"準據",L"准拟", L"準擬",L"准新娘", L"準新娘",L"准新郎", L"準新郎",L"准星", L"準星",L"准是", L"準是",L"准时", L"準時",L"准会", L"準會",L"准决赛", L"準決賽",L"准的", L"準的",L"准确", L"準確",L"准线", L"準線",L"准绳", L"準繩",L"准话", L"準話",L"准谱", L"準譜",L"准货币", L"準貨幣",L"准头", L"準頭",L"准点", L"準點",L"溟蒙", L"溟濛",L"溢于", L"溢於",L"溲面", L"溲麵",L"溺于", L"溺於",L"滃郁", L"滃鬱",L"滑借", L"滑藉",L"汇丰", L"滙豐",L"卤味", L"滷味",L"卤水", L"滷水",L"卤汁", L"滷汁",L"卤湖", L"滷湖",L"卤肉", L"滷肉",L"卤菜", L"滷菜",L"卤蛋", L"滷蛋",L"卤制", L"滷製",L"卤鸡", L"滷雞",L"卤面", L"滷麵",L"满拼自尽", L"滿拚自盡",L"满满当当", L"滿滿當當",L"满头洋发", L"滿頭洋髮",L"漂荡", L"漂蕩",L"漕挽", L"漕輓",L"沤郁", L"漚鬱",L"汉弥登钟", L"漢彌登鐘",L"汉弥登钟表公司", L"漢彌登鐘錶公司",L"漫游", L"漫遊",L"潜意识里", L"潛意識裡",L"潜水表", L"潛水錶",L"潜水钟", L"潛水鐘",L"潜水钟表", L"潛水鐘錶",L"潭里", L"潭裡",L"潮涌", L"潮湧",L"溃于", L"潰於",L"澄澹精致", L"澄澹精致",L"澒蒙", L"澒濛",L"泽渗漓而下降", L"澤滲灕而下降",L"淀乃不耕之地", L"澱乃不耕之地",L"淀北片", L"澱北片",L"淀山", L"澱山",L"淀淀", L"澱澱",L"淀积", L"澱積",L"淀粉", L"澱粉",L"淀解物", L"澱解物",L"淀谓之滓", L"澱謂之滓",L"澹台", L"澹臺",L"澹荡", L"澹蕩",L"激荡", L"激蕩",L"浓发", L"濃髮",L"蒙汜", L"濛汜",L"蒙蒙细雨", L"濛濛細雨",L"蒙雾", L"濛霧",L"蒙松雨", L"濛鬆雨",L"蒙鸿", L"濛鴻",L"泻药", L"瀉藥",L"沈吉线", L"瀋吉線",L"沈山线", L"瀋山線",L"沈州", L"瀋州",L"沈水", L"瀋水",L"沈河", L"瀋河",L"沈海", L"瀋海",L"沈海铁路", L"瀋海鐵路",L"沈阳", L"瀋陽",L"潇洒", L"瀟洒",L"弥山遍野", L"瀰山遍野",L"弥漫", L"瀰漫",L"弥漫着", L"瀰漫著",L"弥弥", L"瀰瀰",L"灌药", L"灌藥",L"漓水", L"灕水",L"漓江", L"灕江",L"漓湘", L"灕湘",L"漓然", L"灕然",L"滩涂", L"灘涂",L"火并非", L"火並非",L"火并", L"火併",L"火拼", L"火拚",L"火折子", L"火摺子",L"火箭布雷", L"火箭佈雷",L"火签", L"火籤",L"火药", L"火藥",L"灰蒙", L"灰濛",L"灰蒙蒙", L"灰濛濛",L"炆面", L"炆麵",L"炒面", L"炒麵",L"炮制", L"炮製",L"炸药", L"炸藥",L"炸酱面", L"炸醬麵",L"为后", L"為後",L"为准", L"為準",L"为着", L"為著",L"乌发", L"烏髮",L"乌龙面", L"烏龍麵",L"烘干", L"烘乾",L"烘制", L"烘製",L"烤干", L"烤乾",L"烤晒", L"烤晒",L"焙干", L"焙乾",L"无后", L"無後",L"无征不信", L"無徵不信",L"无业游民", L"無業游民",L"无梁楼盖", L"無樑樓蓋",L"无法克制", L"無法剋制",L"无药可救", L"無藥可救",L"無言不仇", L"無言不讎",L"无余", L"無餘",L"然后", L"然後",L"然身死才数月耳", L"然身死纔數月耳",L"炼药", L"煉藥",L"炼制", L"煉製",L"煎药", L"煎藥",L"煎面", L"煎麵",L"烟卷", L"煙捲",L"烟斗丝", L"煙斗絲",L"照占", L"照佔",L"照入签", L"照入籤",L"照准", L"照準",L"照相干片", L"照相乾片",L"煨干", L"煨乾",L"煮面", L"煮麵",L"荧郁", L"熒鬱",L"熬药", L"熬藥",L"炖药", L"燉藥",L"燎发", L"燎髮",L"烧干", L"燒乾",L"燕几", L"燕几",L"燕巢于幕", L"燕巢於幕",L"燕燕于飞", L"燕燕于飛",L"燕游", L"燕遊",L"烫一个发", L"燙一個髮",L"烫一次发", L"燙一次髮",L"烫个发", L"燙個髮",L"烫完发", L"燙完髮",L"烫次发", L"燙次髮",L"烫发", L"燙髮",L"烫面", L"燙麵",L"营干", L"營幹",L"烬余", L"燼餘",L"争先恐后", L"爭先恐後",L"争奇斗妍", L"爭奇鬥妍",L"争奇斗异", L"爭奇鬥異",L"争奇斗艳", L"爭奇鬥豔",L"争妍斗奇", L"爭妍鬥奇",L"争妍斗艳", L"爭妍鬥豔",L"争红斗紫", L"爭紅鬥紫",L"争斗", L"爭鬥",L"爰定祥历", L"爰定祥厤",L"爽荡", L"爽蕩",L"尔冬升", L"爾冬陞",L"尔后", L"爾後",L"墙里", L"牆裡",L"片言只语", L"片言隻語",L"牙签", L"牙籤",L"牛后", L"牛後",L"牛肉面", L"牛肉麵",L"牛只", L"牛隻",L"物欲", L"物慾",L"特别致", L"特别致",L"特制住", L"特制住",L"特制定", L"特制定",L"特制止", L"特制止",L"特制订", L"特制訂",L"特征", L"特徵",L"特效药", L"特效藥",L"特制", L"特製",L"牵一发", L"牽一髮",L"牵挂", L"牽挂",L"牵系", L"牽繫",L"荦确", L"犖确",L"狂占", L"狂佔",L"狂并潮", L"狂併潮",L"狃于", L"狃於",L"狐借虎威", L"狐藉虎威",L"猛于", L"猛於",L"猛冲", L"猛衝",L"猜三划五", L"猜三划五",L"犹如表", L"猶如錶",L"犹如钟", L"猶如鐘",L"犹如钟表", L"猶如鐘錶",L"呆串了皮", L"獃串了皮",L"呆事", L"獃事",L"呆人", L"獃人",L"呆子", L"獃子",L"呆性", L"獃性",L"呆想", L"獃想",L"呆憨呆", L"獃憨獃",L"呆根", L"獃根",L"呆气", L"獃氣",L"呆滞", L"獃滯",L"呆呆", L"獃獃",L"呆痴", L"獃痴",L"呆磕", L"獃磕",L"呆等", L"獃等",L"呆脑", L"獃腦",L"呆着", L"獃著",L"呆话", L"獃話",L"呆头", L"獃頭",L"奖杯", L"獎盃",L"独占", L"獨佔",L"独占鳌头", L"獨佔鰲頭",L"独辟蹊径", L"獨闢蹊徑",L"获匪其丑", L"獲匪其醜",L"兽欲", L"獸慾",L"献丑", L"獻醜",L"率团参加", L"率團參加",L"玉历", L"玉曆",L"玉历史", L"玉歷史",L"王庄", L"王莊",L"王余鱼", L"王餘魚",L"珍肴异馔", L"珍肴異饌",L"班里", L"班裡",L"现于", L"現於",L"球杆", L"球杆",L"理一个发", L"理一個髮",L"理一次发", L"理一次髮",L"理个发", L"理個髮",L"理完发", L"理完髮",L"理次发", L"理次髮",L"理发", L"理髮",L"琴钟", L"琴鐘",L"瑞征", L"瑞徵",L"瑶签", L"瑤籤",L"环游", L"環遊",L"瓮安", L"甕安",L"甚于", L"甚於",L"甚么", L"甚麼",L"甜水面", L"甜水麵",L"甜面酱", L"甜麵醬",L"生力面", L"生力麵",L"生于", L"生於",L"生殖洄游", L"生殖洄游",L"生物钟", L"生物鐘",L"生发生", L"生發生",L"生姜", L"生薑",L"生锈", L"生鏽",L"生发", L"生髮",L"产卵洄游", L"產卵洄游",L"产后", L"產後",L"用药", L"用藥",L"甩发", L"甩髮",L"田谷", L"田穀",L"田庄", L"田莊",L"田里", L"田裡",L"由余", L"由余",L"由于", L"由於",L"由表及里", L"由表及裡",L"男佣人", L"男佣人",L"男仆", L"男僕",L"男用表", L"男用錶",L"畏于", L"畏於",L"留后", L"留後",L"留发", L"留髮",L"毕于", L"畢於",L"毕业于", L"畢業於",L"毕生发展", L"畢生發展",L"画着", L"畫著",L"当家才知柴米价", L"當家纔知柴米價",L"当准", L"當準",L"当当丁丁", L"當當丁丁",L"当着", L"當著",L"疏松", L"疏鬆",L"疑系", L"疑係",L"疑凶", L"疑兇",L"疲于", L"疲於",L"疲困", L"疲睏",L"病后", L"病後",L"病后初愈", L"病後初癒",L"病征", L"病徵",L"病愈", L"病癒",L"病余", L"病餘",L"症候群", L"症候群",L"痊愈", L"痊癒",L"痒疹", L"痒疹",L"痒痒", L"痒痒",L"痕迹", L"痕迹",L"愈合", L"癒合",L"症候", L"癥候",L"症状", L"癥狀",L"症结", L"癥結",L"癸丑", L"癸丑",L"发干", L"發乾",L"发汗药", L"發汗藥",L"发呆", L"發獃",L"发蒙", L"發矇",L"发签", L"發籤",L"发庄", L"發莊",L"发着", L"發著",L"发表", L"發表",L"發表", L"發表",L"发松", L"發鬆",L"发面", L"發麵",L"白干", L"白乾",L"白兔擣药", L"白兔擣藥",L"白干儿", L"白干兒",L"白术", L"白朮",L"白朴", L"白樸",L"白净面皮", L"白淨面皮",L"白发其事", L"白發其事",L"白皮松", L"白皮松",L"白粉面", L"白粉麵",L"白里透红", L"白裡透紅",L"白发", L"白髮",L"白胡", L"白鬍",L"白霉", L"白黴",L"百个", L"百個",L"百只可", L"百只可",L"百只够", L"百只夠",L"百只怕", L"百只怕",L"百只足够", L"百只足夠",L"百多只", L"百多隻",L"百天后", L"百天後",L"百拙千丑", L"百拙千醜",L"百科里", L"百科裡",L"百谷", L"百穀",L"百扎", L"百紮",L"百花历", L"百花曆",L"百花历史", L"百花歷史",L"百药之长", L"百藥之長",L"百炼", L"百鍊",L"百只", L"百隻",L"百余", L"百餘",L"的克制", L"的剋制",L"的钟", L"的鐘",L"的钟表", L"的鐘錶",L"皆可作淀", L"皆可作澱",L"皆准", L"皆準",L"皇天后土", L"皇天后土",L"皇历", L"皇曆",L"皇极历", L"皇極曆",L"皇极历史", L"皇極歷史",L"皇历史", L"皇歷史",L"皇庄", L"皇莊",L"皓发", L"皓髮",L"皮制服", L"皮制服",L"皮里春秋", L"皮裡春秋",L"皮里阳秋", L"皮裡陽秋",L"皮制", L"皮製",L"皮松", L"皮鬆",L"皱别", L"皺彆",L"皱折", L"皺摺",L"盆吊", L"盆弔",L"盈余", L"盈餘",L"益于", L"益於",L"盒里", L"盒裡",L"盛赞", L"盛讚",L"盗采", L"盜採",L"盗钟", L"盜鐘",L"尽量克制", L"盡量剋制",L"监制", L"監製",L"盘里", L"盤裡",L"盘回", L"盤迴",L"卢棱伽", L"盧稜伽",L"盲干", L"盲幹",L"直接参与", L"直接參与",L"直于", L"直於",L"直冲", L"直衝",L"相并", L"相併",L"相克制", L"相克制",L"相克服", L"相克服",L"相克", L"相剋",L"相干", L"相干",L"相于", L"相於",L"相冲", L"相衝",L"相斗", L"相鬥",L"看下表", L"看下錶",L"看下钟", L"看下鐘",L"看准", L"看準",L"看着表", L"看著錶",L"看着钟", L"看著鐘",L"看着钟表", L"看著鐘錶",L"看表面", L"看表面",L"看表", L"看錶",L"看钟", L"看鐘",L"真凶", L"真兇",L"真个", L"真箇",L"眼帘", L"眼帘",L"眼眶里", L"眼眶裡",L"眼睛里", L"眼睛裡",L"眼药", L"眼藥",L"眼里", L"眼裡",L"困乏", L"睏乏",L"困倦", L"睏倦",L"困觉", L"睏覺",L"睡着了", L"睡著了",L"睡游病", L"睡遊病",L"瞄准", L"瞄準",L"瞅下表", L"瞅下錶",L"瞅下钟", L"瞅下鐘",L"瞠乎后矣", L"瞠乎後矣",L"瞧着表", L"瞧著錶",L"瞧着钟", L"瞧著鐘",L"瞧着钟表", L"瞧著鐘錶",L"了望", L"瞭望",L"了然", L"瞭然",L"了若指掌", L"瞭若指掌",L"瞳蒙", L"瞳矇",L"瞻前顾后", L"瞻前顧後",L"蒙事", L"矇事",L"蒙昧无知", L"矇昧無知",L"蒙混", L"矇混",L"蒙瞍", L"矇瞍",L"蒙眬", L"矇矓",L"蒙聩", L"矇聵",L"蒙着", L"矇著",L"蒙着锅儿", L"矇著鍋兒",L"蒙头转", L"矇頭轉",L"蒙骗", L"矇騙",L"瞩托", L"矚託",L"矜庄", L"矜莊",L"短几", L"短几",L"短于", L"短於",L"短发", L"短髮",L"矮几", L"矮几",L"石几", L"石几",L"石家庄", L"石家莊",L"石梁", L"石樑",L"石英表", L"石英錶",L"石英钟", L"石英鐘",L"石英钟表", L"石英鐘錶",L"石莼", L"石蓴",L"石钟乳", L"石鐘乳",L"矽谷", L"矽谷",L"研制", L"研製",L"砰当", L"砰噹",L"朱唇皓齿", L"硃唇皓齒",L"朱批", L"硃批",L"朱砂", L"硃砂",L"朱笔", L"硃筆",L"朱红色", L"硃紅色",L"朱色", L"硃色",L"朱谕", L"硃諭",L"硬干", L"硬幹",L"确瘠", L"确瘠",L"碑志", L"碑誌",L"碰钟", L"碰鐘",L"码表", L"碼錶",L"磁制", L"磁製",L"磨制", L"磨製",L"磨炼", L"磨鍊",L"磬钟", L"磬鐘",L"硗确", L"磽确",L"碍难照准", L"礙難照准",L"砻谷机", L"礱穀機",L"示范", L"示範",L"社里", L"社裡",L"祝赞", L"祝讚",L"祝发", L"祝髮",L"神荼郁垒", L"神荼鬱壘",L"神游", L"神遊",L"神雕像", L"神雕像",L"神雕", L"神鵰",L"票庄", L"票莊",L"祭吊", L"祭弔",L"祭吊文", L"祭弔文",L"禁欲", L"禁慾",L"禁欲主义", L"禁欲主義",L"禁药", L"禁藥",L"祸于", L"禍於",L"御侮", L"禦侮",L"御寇", L"禦寇",L"御寒", L"禦寒",L"御敌", L"禦敵",L"礼赞", L"禮讚",L"禹余粮", L"禹餘糧",L"禾谷", L"禾穀",L"秃妃之发", L"禿妃之髮",L"秃发", L"禿髮",L"秀发", L"秀髮",L"私下里", L"私下裡",L"私欲", L"私慾",L"私斗", L"私鬥",L"秋假里", L"秋假裡",L"秋天里", L"秋天裡",L"秋后", L"秋後",L"秋日里", L"秋日裡",L"秋裤", L"秋褲",L"秋游", L"秋遊",L"秋阴入井干", L"秋陰入井幹",L"秋发", L"秋髮",L"种师中", L"种師中",L"种师道", L"种師道",L"种放", L"种放",L"科斗", L"科斗",L"科范", L"科範",L"秒表明", L"秒表明",L"秒表示", L"秒表示",L"秒表", L"秒錶",L"秒钟", L"秒鐘",L"移祸于", L"移禍於",L"稀松", L"稀鬆",L"税后", L"稅後",L"税后净利", L"稅後淨利",L"稍后", L"稍後",L"棱台", L"稜台",L"棱子", L"稜子",L"棱层", L"稜層",L"棱柱", L"稜柱",L"棱登", L"稜登",L"棱棱", L"稜稜",L"棱等登", L"稜等登",L"棱线", L"稜線",L"棱缝", L"稜縫",L"棱角", L"稜角",L"棱锥", L"稜錐",L"棱镜", L"稜鏡",L"棱体", L"稜體",L"种谷", L"種穀",L"称赞", L"稱讚",L"稻谷", L"稻穀",L"稽征", L"稽徵",L"谷人", L"穀人",L"谷保家商", L"穀保家商",L"谷仓", L"穀倉",L"谷圭", L"穀圭",L"谷场", L"穀場",L"谷子", L"穀子",L"谷日", L"穀日",L"谷旦", L"穀旦",L"谷梁", L"穀梁",L"谷壳", L"穀殼",L"谷物", L"穀物",L"谷皮", L"穀皮",L"谷神", L"穀神",L"谷谷", L"穀穀",L"谷米", L"穀米",L"谷粒", L"穀粒",L"谷舱", L"穀艙",L"谷苗", L"穀苗",L"谷草", L"穀草",L"谷贵饿农", L"穀貴餓農",L"谷贱伤农", L"穀賤傷農",L"谷道", L"穀道",L"谷雨", L"穀雨",L"谷类", L"穀類",L"谷食", L"穀食",L"穆罕默德历", L"穆罕默德曆",L"穆罕默德历史", L"穆罕默德歷史",L"积极参与", L"積极參与",L"积极参加", L"積极參加",L"积淀", L"積澱",L"积谷", L"積穀",L"积谷防饥", L"積穀防饑",L"积郁", L"積鬱",L"稳占", L"穩佔",L"稳扎", L"穩紮",L"空中布雷", L"空中佈雷",L"空投布雷", L"空投佈雷",L"空蒙", L"空濛",L"空荡", L"空蕩",L"空荡荡", L"空蕩蕩",L"空谷回音", L"空谷回音",L"空钟", L"空鐘",L"空余", L"空餘",L"窒欲", L"窒慾",L"窗台上", L"窗台上",L"窗帘", L"窗帘",L"窗明几亮", L"窗明几亮",L"窗明几净", L"窗明几淨",L"窗台", L"窗檯",L"窝里", L"窩裡",L"穷于", L"窮於",L"穷追不舍", L"窮追不捨",L"穷发", L"窮髮",L"窃钟掩耳", L"竊鐘掩耳",L"立于", L"立於",L"立范", L"立範",L"站干岸儿", L"站乾岸兒",L"童仆", L"童僕",L"端庄", L"端莊",L"竞斗", L"競鬥",L"竹几", L"竹几",L"竹林之游", L"竹林之遊",L"竹签", L"竹籤",L"笑里藏刀", L"笑裡藏刀",L"笨笨呆呆", L"笨笨呆呆",L"第四出局", L"第四出局",L"笔划", L"筆劃",L"笔秃墨干", L"筆禿墨乾",L"等于", L"等於",L"笋干", L"筍乾",L"筑前", L"筑前",L"筑北", L"筑北",L"筑州", L"筑州",L"筑後", L"筑後",L"筑后", L"筑後",L"筑波", L"筑波",L"筑紫", L"筑紫",L"筑肥", L"筑肥",L"筑西", L"筑西",L"筑邦", L"筑邦",L"筑陽", L"筑陽",L"筑阳", L"筑陽",L"答复", L"答覆",L"答覆", L"答覆",L"策划", L"策劃",L"筵几", L"筵几",L"个中原因", L"箇中原因",L"个中奥妙", L"箇中奧妙",L"个中奥秘", L"箇中奧秘",L"个中好手", L"箇中好手",L"个中强手", L"箇中強手",L"个中消息", L"箇中消息",L"个中滋味", L"箇中滋味",L"个中玄机", L"箇中玄機",L"个中理由", L"箇中理由",L"个中讯息", L"箇中訊息",L"个中资讯", L"箇中資訊",L"个中高手", L"箇中高手",L"个旧", L"箇舊",L"算历", L"算曆",L"算历史", L"算歷史",L"算准", L"算準",L"算发", L"算髮",L"管人吊脚儿事", L"管人弔腳兒事",L"管制法", L"管制法",L"管干", L"管幹",L"节欲", L"節慾",L"节余", L"節餘",L"范例", L"範例",L"范围", L"範圍",L"范字", L"範字",L"范式", L"範式",L"范性形变", L"範性形變",L"范文", L"範文",L"范本", L"範本",L"范畴", L"範疇",L"范金", L"範金",L"简并", L"簡併",L"简朴", L"簡樸",L"簸荡", L"簸蕩",L"签着", L"簽著",L"筹划", L"籌劃",L"签幐", L"籤幐",L"签押", L"籤押",L"签条", L"籤條",L"签诗", L"籤詩",L"吁天", L"籲天",L"吁求", L"籲求",L"吁请", L"籲請",L"米谷", L"米穀",L"粉拳绣腿", L"粉拳繡腿",L"粉签子", L"粉籤子",L"粗制", L"粗製",L"精制伏", L"精制伏",L"精制住", L"精制住",L"精制服", L"精制服",L"精干", L"精幹",L"精于", L"精於",L"精准", L"精準",L"精致", L"精緻",L"精制", L"精製",L"精炼", L"精鍊",L"精辟", L"精闢",L"精松", L"精鬆",L"糊里糊涂", L"糊裡糊塗",L"糕干", L"糕乾",L"粪秽蔑面", L"糞穢衊面",L"团子", L"糰子",L"系着", L"系著",L"系里", L"系裡",L"纪元后", L"紀元後",L"纪历", L"紀曆",L"纪历史", L"紀歷史",L"约占", L"約佔",L"红绳系足", L"紅繩繫足",L"红钟", L"紅鐘",L"红霉素", L"紅霉素",L"红发", L"紅髮",L"纡回", L"紆迴",L"纡余", L"紆餘",L"纡郁", L"紆鬱",L"纳征", L"納徵",L"纯朴", L"純樸",L"纸扎", L"紙紮",L"素朴", L"素樸",L"素发", L"素髮",L"素面", L"素麵",L"索马里", L"索馬里",L"索馬里", L"索馬里",L"索面", L"索麵",L"紫姜", L"紫薑",L"扎上", L"紮上",L"扎下", L"紮下",L"扎囮", L"紮囮",L"扎好", L"紮好",L"扎实", L"紮實",L"扎寨", L"紮寨",L"扎带子", L"紮帶子",L"扎成", L"紮成",L"扎根", L"紮根",L"扎营", L"紮營",L"扎紧", L"紮緊",L"扎脚", L"紮腳",L"扎裹", L"紮裹",L"扎诈", L"紮詐",L"扎起", L"紮起",L"扎铁", L"紮鐵",L"细不容发", L"細不容髮",L"细如发", L"細如髮",L"细致", L"細緻",L"细炼", L"細鍊",L"终于", L"終於",L"组里", L"組裡",L"结伴同游", L"結伴同遊",L"结伙", L"結夥",L"结扎", L"結紮",L"结彩", L"結綵",L"结余", L"結餘",L"结发", L"結髮",L"绝对参照", L"絕對參照",L"绝后", L"絕後",L"绝于", L"絕於",L"绞干", L"絞乾",L"络腮胡", L"絡腮鬍",L"给我干脆", L"給我干脆",L"给于", L"給於",L"丝来线去", L"絲來線去",L"丝布", L"絲布",L"丝恩发怨", L"絲恩髮怨",L"丝板", L"絲板",L"丝瓜布", L"絲瓜布",L"丝绒布", L"絲絨布",L"丝线", L"絲線",L"丝织厂", L"絲織廠",L"丝虫", L"絲蟲",L"丝发", L"絲髮",L"绑扎", L"綁紮",L"綑扎", L"綑紮",L"经有云", L"經有云",L"經有云", L"經有云",L"绿发", L"綠髮",L"绸缎庄", L"綢緞莊",L"维系", L"維繫",L"绾发", L"綰髮",L"网里", L"網裡",L"网志", L"網誌",L"彩带", L"綵帶",L"彩排", L"綵排",L"彩楼", L"綵樓",L"彩牌楼", L"綵牌樓",L"彩球", L"綵球",L"彩绸", L"綵綢",L"彩线", L"綵線",L"彩船", L"綵船",L"彩衣", L"綵衣",L"紧致", L"緊緻",L"紧绷", L"緊繃",L"紧绷绷", L"緊繃繃",L"紧绷着", L"緊繃著",L"紧追不舍", L"緊追不捨",L"绪余", L"緒餘",L"緝凶", L"緝兇",L"缉凶", L"緝兇",L"编余", L"編余",L"编制法", L"編制法",L"编采", L"編採",L"编码表", L"編碼表",L"编制", L"編製",L"编钟", L"編鐘",L"编发", L"編髮",L"缓征", L"緩徵",L"缓冲", L"緩衝",L"致密", L"緻密",L"萦回", L"縈迴",L"缜致", L"縝緻",L"县里", L"縣裡",L"县志", L"縣誌",L"缝里", L"縫裡",L"缝制", L"縫製",L"缩栗", L"縮慄",L"纵欲", L"縱慾",L"纤夫", L"縴夫",L"纤手", L"縴手",L"总后", L"總後",L"总裁制", L"總裁制",L"繁复", L"繁複",L"繁钟", L"繁鐘",L"绷住", L"繃住",L"绷子", L"繃子",L"绷带", L"繃帶",L"绷扒吊拷", L"繃扒弔拷",L"绷紧", L"繃緊",L"绷脸", L"繃臉",L"绷着", L"繃著",L"绷着脸", L"繃著臉",L"绷着脸儿", L"繃著臉兒",L"绷开", L"繃開",L"穗帏飘井干", L"繐幃飄井幹",L"绕梁", L"繞樑",L"绣像", L"繡像",L"绣口", L"繡口",L"绣得", L"繡得",L"绣户", L"繡戶",L"绣房", L"繡房",L"绣毯", L"繡毯",L"绣球", L"繡球",L"绣的", L"繡的",L"绣花", L"繡花",L"绣衣", L"繡衣",L"绣起", L"繡起",L"绣阁", L"繡閣",L"绣鞋", L"繡鞋",L"绘制", L"繪製",L"系上", L"繫上",L"系世", L"繫世",L"系到", L"繫到",L"系囚", L"繫囚",L"系心", L"繫心",L"系念", L"繫念",L"系怀", L"繫懷",L"系恋", L"繫戀",L"系于", L"繫於",L"系于一发", L"繫於一髮",L"系结", L"繫結",L"系紧", L"繫緊",L"系绳", L"繫繩",L"系累", L"繫纍",L"系辞", L"繫辭",L"系风捕影", L"繫風捕影",L"累囚", L"纍囚",L"累堆", L"纍堆",L"累瓦结绳", L"纍瓦結繩",L"累绁", L"纍紲",L"累臣", L"纍臣",L"缠斗", L"纏鬥",L"才则", L"纔則",L"才可容颜十五余", L"纔可容顏十五餘",L"才得两年", L"纔得兩年",L"才此", L"纔此",L"坛子", L"罈子",L"坛坛罐罐", L"罈罈罐罐",L"坛騞", L"罈騞",L"置于", L"置於",L"置言成范", L"置言成範",L"骂着", L"罵著",L"罢于", L"罷於",L"羁系", L"羈繫",L"美占", L"美佔",L"美仑", L"美崙",L"美于", L"美於",L"美制", L"美製",L"美丑", L"美醜",L"美发", L"美髮",L"群丑", L"群醜",L"羡余", L"羨餘",L"义占", L"義佔",L"义仆", L"義僕",L"义庄", L"義莊",L"翕辟", L"翕闢",L"翱游", L"翱遊",L"翻涌", L"翻湧",L"翻云覆雨", L"翻雲覆雨",L"翻松", L"翻鬆",L"老干", L"老乾",L"老仆", L"老僕",L"老干部", L"老幹部",L"老蒙", L"老懞",L"老于", L"老於",L"老爷钟", L"老爺鐘",L"老庄", L"老莊",L"老姜", L"老薑",L"老板", L"老闆",L"老面皮", L"老面皮",L"考后", L"考後",L"考征", L"考徵",L"而克制", L"而剋制",L"而后", L"而後",L"耍斗", L"耍鬥",L"耕佣", L"耕傭",L"耕获", L"耕穫",L"耳后", L"耳後",L"耳余", L"耳餘",L"耿于", L"耿於",L"聊斋志异", L"聊齋志異",L"聘雇", L"聘僱",L"联系", L"聯繫",L"听于", L"聽於",L"肉干", L"肉乾",L"肉欲", L"肉慾",L"肉丝面", L"肉絲麵",L"肉羹面", L"肉羹麵",L"肉松", L"肉鬆",L"肚里", L"肚裡",L"肝脏", L"肝臟",L"肝郁", L"肝鬱",L"股栗", L"股慄",L"肥筑方言", L"肥筑方言",L"肴馔", L"肴饌",L"肺脏", L"肺臟",L"胃药", L"胃藥",L"胃里", L"胃裡",L"背向着", L"背向著",L"背地里", L"背地裡",L"背后", L"背後",L"胎发", L"胎髮",L"胜肽", L"胜肽",L"胜键", L"胜鍵",L"胡云", L"胡云",L"胡子昂", L"胡子昂",L"胡朴安", L"胡樸安",L"胡里胡涂", L"胡裡胡塗",L"能克制", L"能剋制",L"能干休", L"能干休",L"能干戈", L"能干戈",L"能干扰", L"能干擾",L"能干政", L"能干政",L"能干涉", L"能干涉",L"能干预", L"能干預",L"能干", L"能幹",L"能自制", L"能自制",L"脉冲", L"脈衝",L"脊梁背", L"脊梁背",L"脊梁骨", L"脊梁骨",L"脊梁", L"脊樑",L"脱谷机", L"脫穀機",L"脱发", L"脫髮",L"脾脏", L"脾臟",L"腊之以为饵", L"腊之以為餌",L"腊味", L"腊味",L"腊毒", L"腊毒",L"腊笔", L"腊筆",L"肾脏", L"腎臟",L"腐干", L"腐乾",L"腐余", L"腐餘",L"腕表", L"腕錶",L"脑子里", L"腦子裡",L"脑干", L"腦幹",L"脑后", L"腦後",L"腰里", L"腰裡",L"脚注", L"腳註",L"脚炼", L"腳鍊",L"膏药", L"膏藥",L"肤发", L"膚髮",L"胶卷", L"膠捲",L"膨松", L"膨鬆",L"臣仆", L"臣僕",L"卧游", L"臥遊",L"臧谷亡羊", L"臧穀亡羊",L"临潼斗宝", L"臨潼鬥寶",L"自制一下", L"自制一下",L"自制下来", L"自制下來",L"自制不", L"自制不",L"自制之力", L"自制之力",L"自制之能", L"自制之能",L"自制他", L"自制他",L"自制伏", L"自制伏",L"自制你", L"自制你",L"自制力", L"自制力",L"自制地", L"自制地",L"自制她", L"自制她",L"自制情", L"自制情",L"自制我", L"自制我",L"自制服", L"自制服",L"自制的能", L"自制的能",L"自制能力", L"自制能力",L"自于", L"自於",L"自制", L"自製",L"自觉自愿", L"自覺自愿",L"至多", L"至多",L"至于", L"至於",L"致于", L"致於",L"臻于", L"臻於",L"舂谷", L"舂穀",L"与克制", L"與剋制",L"兴致", L"興緻",L"举手表", L"舉手表",L"举手表决", L"舉手表決",L"旧庄", L"舊庄",L"旧历", L"舊曆",L"旧历史", L"舊歷史",L"旧药", L"舊藥",L"旧游", L"舊遊",L"旧表", L"舊錶",L"旧钟", L"舊鐘",L"旧钟表", L"舊鐘錶",L"舌干唇焦", L"舌乾唇焦",L"舌后", L"舌後",L"舒卷", L"舒捲",L"航海历", L"航海曆",L"航海历史", L"航海歷史",L"船只得", L"船只得",L"船只有", L"船只有",L"船只能", L"船只能",L"船钟", L"船鐘",L"船只", L"船隻",L"舰只", L"艦隻",L"良药", L"良藥",L"色欲", L"色慾",L"艸木丰丰", L"艸木丰丰",L"芍药", L"芍藥",L"芒果干", L"芒果乾",L"花拳绣腿", L"花拳繡腿",L"花卷", L"花捲",L"花盆里", L"花盆裡",L"花庵词选", L"花菴詞選",L"花药", L"花藥",L"花钟", L"花鐘",L"花马吊嘴", L"花馬弔嘴",L"花哄", L"花鬨",L"苑里", L"苑裡",L"若干", L"若干",L"苦干", L"苦幹",L"苦药", L"苦藥",L"苦里", L"苦裡",L"苦斗", L"苦鬥",L"苎麻", L"苧麻",L"英占", L"英佔",L"苹萦", L"苹縈",L"茂都淀", L"茂都澱",L"范文同", L"范文同",L"范文正公", L"范文正公",L"范文瀾", L"范文瀾",L"范文澜", L"范文瀾",L"范文照", L"范文照",L"范文程", L"范文程",L"范文芳", L"范文芳",L"范文藤", L"范文藤",L"范文虎", L"范文虎",L"范登堡", L"范登堡",L"茶几", L"茶几",L"茶庄", L"茶莊",L"茶余", L"茶餘",L"茶面", L"茶麵",L"草丛里", L"草叢裡",L"草广", L"草广",L"草荐", L"草荐",L"草药", L"草藥",L"荐居", L"荐居",L"荐臻", L"荐臻",L"荐饥", L"荐饑",L"荷花淀", L"荷花澱",L"庄上", L"莊上",L"庄主", L"莊主",L"庄周", L"莊周",L"庄员", L"莊員",L"庄严", L"莊嚴",L"庄园", L"莊園",L"庄士顿道", L"莊士頓道",L"庄子", L"莊子",L"庄客", L"莊客",L"庄家", L"莊家",L"庄户", L"莊戶",L"庄房", L"莊房",L"庄敬", L"莊敬",L"庄田", L"莊田",L"庄稼", L"莊稼",L"庄舄越吟", L"莊舄越吟",L"庄里", L"莊裡",L"庄语", L"莊語",L"庄农", L"莊農",L"庄重", L"莊重",L"庄院", L"莊院",L"庄骚", L"莊騷",L"茎干", L"莖幹",L"莽荡", L"莽蕩",L"菌丝体", L"菌絲體",L"菜干", L"菜乾",L"菜肴", L"菜肴",L"菠棱菜", L"菠稜菜",L"菠萝干", L"菠蘿乾",L"华严钟", L"華嚴鐘",L"华发", L"華髮",L"万一只", L"萬一只",L"万个", L"萬個",L"万多只", L"萬多隻",L"万天后", L"萬天後",L"万年历表", L"萬年曆錶",L"万历", L"萬曆",L"万历史", L"萬歷史",L"万签插架", L"萬籤插架",L"万扎", L"萬紮",L"万象", L"萬象",L"万只", L"萬隻",L"万余", L"萬餘",L"落后", L"落後",L"落腮胡", L"落腮鬍",L"落发", L"落髮",L"叶叶琹", L"葉叶琹",L"着儿", L"著兒",L"着克制", L"著剋制",L"着书立说", L"著書立說",L"着色软体", L"著色軟體",L"着重指出", L"著重指出",L"着录", L"著錄",L"着录规则", L"著錄規則",L"葡占", L"葡佔",L"葡萄干", L"葡萄乾",L"董氏封发", L"董氏封髮",L"葫芦里卖甚么药", L"葫蘆裡賣甚麼藥",L"蒙汗药", L"蒙汗藥",L"蒙庄", L"蒙莊",L"蒙雾露", L"蒙霧露",L"蒜发", L"蒜髮",L"苍术", L"蒼朮",L"苍发", L"蒼髮",L"苍郁", L"蒼鬱",L"蓄发", L"蓄髮",L"蓄胡", L"蓄鬍",L"蓄须", L"蓄鬚",L"蓊郁", L"蓊鬱",L"蓬蓬松松", L"蓬蓬鬆鬆",L"蓬发", L"蓬髮",L"蓬松", L"蓬鬆",L"参绥", L"蔘綏",L"葱郁", L"蔥鬱",L"荞麦面", L"蕎麥麵",L"荡来荡去", L"蕩來蕩去",L"荡女", L"蕩女",L"荡妇", L"蕩婦",L"荡寇", L"蕩寇",L"荡平", L"蕩平",L"荡气回肠", L"蕩氣迴腸",L"荡涤", L"蕩滌",L"荡漾", L"蕩漾",L"荡然", L"蕩然",L"荡产", L"蕩產",L"荡舟", L"蕩舟",L"荡船", L"蕩船",L"荡荡", L"蕩蕩",L"萧参", L"蕭蔘",L"薄幸", L"薄倖",L"薄干", L"薄幹",L"姜是老的辣", L"薑是老的辣",L"姜末", L"薑末",L"姜桂", L"薑桂",L"姜母", L"薑母",L"姜汁", L"薑汁",L"姜汤", L"薑湯",L"姜片", L"薑片",L"姜糖", L"薑糖",L"姜丝", L"薑絲",L"姜老辣", L"薑老辣",L"姜茶", L"薑茶",L"姜蓉", L"薑蓉",L"姜饼", L"薑餅",L"姜黄", L"薑黃",L"薙发", L"薙髮",L"薝卜", L"薝蔔",L"苧悴", L"薴悴",L"苧烯", L"薴烯",L"薴烯", L"薴烯",L"借以", L"藉以",L"借助", L"藉助",L"借寇兵", L"藉寇兵",L"借手", L"藉手",L"借机", L"藉機",L"借此", L"藉此",L"借由", L"藉由",L"借箸代筹", L"藉箸代籌",L"借着", L"藉著",L"借资", L"藉資",L"蓝淀", L"藍澱",L"藏于", L"藏於",L"藏历", L"藏曆",L"藏历史", L"藏歷史",L"藏蒙歌儿", L"藏矇歌兒",L"藤制", L"藤製",L"药丸", L"藥丸",L"药典", L"藥典",L"药到命除", L"藥到命除",L"药到病除", L"藥到病除",L"药剂", L"藥劑",L"药力", L"藥力",L"药包", L"藥包",L"药名", L"藥名",L"药味", L"藥味",L"药品", L"藥品",L"药商", L"藥商",L"药单", L"藥單",L"药婆", L"藥婆",L"药学", L"藥學",L"药害", L"藥害",L"药专", L"藥專",L"药局", L"藥局",L"药师", L"藥師",L"药店", L"藥店",L"药厂", L"藥廠",L"药引", L"藥引",L"药性", L"藥性",L"药房", L"藥房",L"药效", L"藥效",L"药方", L"藥方",L"药材", L"藥材",L"药棉", L"藥棉",L"药检局", L"藥檢局",L"药水", L"藥水",L"药油", L"藥油",L"药液", L"藥液",L"药渣", L"藥渣",L"药片", L"藥片",L"药物", L"藥物",L"药王", L"藥王",L"药理", L"藥理",L"药瓶", L"藥瓶",L"药用", L"藥用",L"药皂", L"藥皂",L"药盒", L"藥盒",L"药石", L"藥石",L"药科", L"藥科",L"药箱", L"藥箱",L"药签", L"藥籤",L"药粉", L"藥粉",L"药糖", L"藥糖",L"药线", L"藥線",L"药罐", L"藥罐",L"药膏", L"藥膏",L"药舖", L"藥舖",L"药茶", L"藥茶",L"药草", L"藥草",L"药行", L"藥行",L"药贩", L"藥販",L"药费", L"藥費",L"药酒", L"藥酒",L"药医学系", L"藥醫學系",L"药量", L"藥量",L"药针", L"藥針",L"药铺", L"藥鋪",L"药头", L"藥頭",L"药饵", L"藥餌",L"药面儿", L"藥麵兒",L"苏昆", L"蘇崑",L"蕴含着", L"蘊含著",L"蕴涵着", L"蘊涵著",L"苹果干", L"蘋果乾",L"萝卜", L"蘿蔔",L"萝卜干", L"蘿蔔乾",L"虎须", L"虎鬚",L"虎斗", L"虎鬥",L"号志", L"號誌",L"虫部", L"虫部",L"蚊动牛斗", L"蚊動牛鬥",L"蛇发女妖", L"蛇髮女妖",L"蛔虫药", L"蛔蟲藥",L"蜂涌", L"蜂湧",L"蜂准", L"蜂準",L"蜜里调油", L"蜜裡調油",L"蜡月", L"蜡月",L"蜡祭", L"蜡祭",L"蝎蝎螫螫", L"蝎蝎螫螫",L"蝎谮", L"蝎譖",L"虮蝨相吊", L"蟣蝨相弔",L"蛏干", L"蟶乾",L"蠁干", L"蠁幹",L"蛮干", L"蠻幹",L"血拼", L"血拚",L"血余", L"血餘",L"行事历", L"行事曆",L"行事历史", L"行事歷史",L"行凶", L"行兇",L"行凶前", L"行兇前",L"行凶后", L"行兇後",L"行凶後", L"行兇後",L"行于", L"行於",L"行百里者半于九十", L"行百里者半於九十",L"胡同", L"衚衕",L"卫星钟", L"衛星鐘",L"冲上", L"衝上",L"冲下", L"衝下",L"冲来", L"衝來",L"冲倒", L"衝倒",L"冲冠", L"衝冠",L"冲出", L"衝出",L"冲到", L"衝到",L"冲刺", L"衝刺",L"冲克", L"衝剋",L"冲力", L"衝力",L"冲劲", L"衝勁",L"冲动", L"衝動",L"冲去", L"衝去",L"冲口", L"衝口",L"冲垮", L"衝垮",L"冲堂", L"衝堂",L"冲坚陷阵", L"衝堅陷陣",L"冲压", L"衝壓",L"冲天", L"衝天",L"冲州撞府", L"衝州撞府",L"冲心", L"衝心",L"冲掉", L"衝掉",L"冲撞", L"衝撞",L"冲击", L"衝擊",L"冲散", L"衝散",L"冲杀", L"衝殺",L"冲决", L"衝決",L"冲波", L"衝波",L"冲浪", L"衝浪",L"冲激", L"衝激",L"冲然", L"衝然",L"冲盹", L"衝盹",L"冲破", L"衝破",L"冲程", L"衝程",L"冲突", L"衝突",L"冲线", L"衝線",L"冲着", L"衝著",L"冲要", L"衝要",L"冲起", L"衝起",L"冲车", L"衝車",L"冲进", L"衝進",L"冲过", L"衝過",L"冲量", L"衝量",L"冲锋", L"衝鋒",L"冲陷", L"衝陷",L"冲头阵", L"衝頭陣",L"冲风", L"衝風",L"衣绣昼行", L"衣繡晝行",L"表征", L"表徵",L"表里", L"表裡",L"表面", L"表面",L"衷于", L"衷於",L"袋里", L"袋裡",L"袋表", L"袋錶",L"袖里", L"袖裡",L"被里", L"被裡",L"被复", L"被複",L"被覆着", L"被覆著",L"被发佯狂", L"被髮佯狂",L"被发入山", L"被髮入山",L"被发左衽", L"被髮左衽",L"被发缨冠", L"被髮纓冠",L"被发阳狂", L"被髮陽狂",L"裁并", L"裁併",L"裁制", L"裁製",L"里手", L"裏手",L"里海", L"裏海",L"补于", L"補於",L"补药", L"補藥",L"补血药", L"補血藥",L"补注", L"補註",L"装折", L"裝摺",L"里勾外连", L"裡勾外連",L"里外", L"裡外",L"里子", L"裡子",L"里屋", L"裡屋",L"里层", L"裡層",L"里布", L"裡布",L"里带", L"裡帶",L"里弦", L"裡弦",L"里应外合", L"裡應外合",L"里脊", L"裡脊",L"里衣", L"裡衣",L"里通外国", L"裡通外國",L"里通外敌", L"裡通外敵",L"里边", L"裡邊",L"里间", L"裡間",L"里面", L"裡面",L"里面包", L"裡面包",L"里头", L"裡頭",L"制件", L"製件",L"制作", L"製作",L"制做", L"製做",L"制备", L"製備",L"制冰", L"製冰",L"制冷", L"製冷",L"制剂", L"製劑",L"制取", L"製取",L"制品", L"製品",L"制图", L"製圖",L"制得", L"製得",L"制成", L"製成",L"制法", L"製法",L"制浆", L"製漿",L"制为", L"製為",L"制片", L"製片",L"制版", L"製版",L"制程", L"製程",L"制糖", L"製糖",L"制纸", L"製紙",L"制药", L"製藥",L"制表", L"製表",L"制造", L"製造",L"制革", L"製革",L"制鞋", L"製鞋",L"制盐", L"製鹽",L"复仞年如", L"複仞年如",L"复以百万", L"複以百萬",L"复位", L"複位",L"复信", L"複信",L"复元音", L"複元音",L"复函数", L"複函數",L"复分数", L"複分數",L"复分析", L"複分析",L"复分解", L"複分解",L"复列", L"複列",L"复利", L"複利",L"复印", L"複印",L"复句", L"複句",L"复合", L"複合",L"复名", L"複名",L"复员", L"複員",L"复壁", L"複壁",L"复壮", L"複壯",L"复姓", L"複姓",L"复字键", L"複字鍵",L"复审", L"複審",L"复写", L"複寫",L"复对数", L"複對數",L"复平面", L"複平面",L"复式", L"複式",L"复复", L"複復",L"复数", L"複數",L"复本", L"複本",L"复查", L"複查",L"复核", L"複核",L"复检", L"複檢",L"复次", L"複次",L"复比", L"複比",L"复决", L"複決",L"复流", L"複流",L"复测", L"複測",L"复亩珍", L"複畝珍",L"复发", L"複發",L"复目", L"複目",L"复眼", L"複眼",L"复种", L"複種",L"复线", L"複線",L"复习", L"複習",L"复色", L"複色",L"复叶", L"複葉",L"复制", L"複製",L"复诊", L"複診",L"复评", L"複評",L"复词", L"複詞",L"复试", L"複試",L"复课", L"複課",L"复议", L"複議",L"复变函数", L"複變函數",L"复赛", L"複賽",L"复辅音", L"複輔音",L"复述", L"複述",L"复选", L"複選",L"复钱", L"複錢",L"复阅", L"複閱",L"复杂", L"複雜",L"复电", L"複電",L"复音", L"複音",L"复韵", L"複韻",L"褒赞", L"褒讚",L"衬里", L"襯裡",L"西占", L"西佔",L"西元后", L"西元後",L"西周钟", L"西周鐘",L"西岳", L"西嶽",L"西晒", L"西晒",L"西历", L"西曆",L"西历史", L"西歷史",L"西米谷", L"西米谷",L"西药", L"西藥",L"西谷米", L"西谷米",L"西游", L"西遊",L"要占", L"要佔",L"要克制", L"要剋制",L"要占卜", L"要占卜",L"要自制", L"要自制",L"要冲", L"要衝",L"要么", L"要麼",L"覆亡", L"覆亡",L"覆命", L"覆命",L"覆巢之下无完卵", L"覆巢之下無完卵",L"覆水难收", L"覆水難收",L"覆没", L"覆沒",L"覆着", L"覆著",L"覆盖", L"覆蓋",L"覆盖着", L"覆蓋著",L"覆辙", L"覆轍",L"覆雨翻云", L"覆雨翻雲",L"见于", L"見於",L"见棱见角", L"見稜見角",L"见素抱朴", L"見素抱樸",L"见钟不打", L"見鐘不打",L"规划", L"規劃",L"规范", L"規範",L"視如寇仇", L"視如寇讎",L"视于", L"視於",L"观采", L"觀採",L"角落发", L"角落發",L"角落里", L"角落裡",L"觚棱", L"觚稜",L"解雇", L"解僱",L"解痛药", L"解痛藥",L"解药", L"解藥",L"解铃仍须系铃人", L"解鈴仍須繫鈴人",L"解铃还须系铃人", L"解鈴還須繫鈴人",L"解发佯狂", L"解髮佯狂",L"触须", L"觸鬚",L"言云", L"言云",L"言大而夸", L"言大而夸",L"言辩而确", L"言辯而确",L"订制", L"訂製",L"计划", L"計劃",L"计时表", L"計時錶",L"托了", L"託了",L"托事", L"託事",L"托交", L"託交",L"托人", L"託人",L"托付", L"託付",L"托儿所", L"託兒所",L"托古讽今", L"託古諷今",L"托名", L"託名",L"托命", L"託命",L"托咎", L"託咎",L"托梦", L"託夢",L"托大", L"託大",L"托孤", L"託孤",L"托庇", L"託庇",L"托故", L"託故",L"托疾", L"託疾",L"托病", L"託病",L"托管", L"託管",L"托言", L"託言",L"托词", L"託詞",L"托买", L"託買",L"托卖", L"託賣",L"托身", L"託身",L"托辞", L"託辭",L"托运", L"託運",L"托过", L"託過",L"托附", L"託附",L"许愿起经", L"許愿起經",L"诉说着", L"訴說著",L"注上", L"註上",L"注册", L"註冊",L"注失", L"註失",L"注定", L"註定",L"注明", L"註明",L"注标", L"註標",L"注生娘娘", L"註生娘娘",L"注疏", L"註疏",L"注脚", L"註腳",L"注解", L"註解",L"注记", L"註記",L"注译", L"註譯",L"注销", L"註銷",L"注：", L"註：",L"评断发", L"評斷發",L"评注", L"評註",L"词干", L"詞幹",L"词汇", L"詞彙",L"词余", L"詞餘",L"询于", L"詢於",L"询于刍荛", L"詢於芻蕘",L"试药", L"試藥",L"试制", L"試製",L"诗云", L"詩云",L"詩云", L"詩云",L"诗赞", L"詩讚",L"诗钟", L"詩鐘",L"诗余", L"詩餘",L"话里有话", L"話裡有話",L"该钟", L"該鐘",L"详征博引", L"詳徵博引",L"详注", L"詳註",L"诔赞", L"誄讚",L"夸多斗靡", L"誇多鬥靡",L"夸能斗智", L"誇能鬥智",L"夸赞", L"誇讚",L"志哀", L"誌哀",L"志喜", L"誌喜",L"志庆", L"誌慶",L"志异", L"誌異",L"认准", L"認準",L"诱奸", L"誘姦",L"语云", L"語云",L"语汇", L"語彙",L"语有云", L"語有云",L"語有云", L"語有云",L"诚征", L"誠徵",L"诚朴", L"誠樸",L"诬蔑", L"誣衊",L"说着", L"說著",L"谁干的", L"誰幹的",L"课后", L"課後",L"课征", L"課徵",L"课余", L"課餘",L"调准", L"調準",L"调制", L"調製",L"调表", L"調錶",L"调钟表", L"調鐘錶",L"谈征", L"談徵",L"请参阅", L"請參閱",L"请君入瓮", L"請君入甕",L"请托", L"請託",L"咨询", L"諮詢",L"诸余", L"諸餘",L"谋定后动", L"謀定後動",L"谋干", L"謀幹",L"谢绝参观", L"謝絕參觀",L"谬采虚声", L"謬採虛聲",L"谬赞", L"謬讚",L"謷丑", L"謷醜",L"谨于心", L"謹於心",L"警世钟", L"警世鐘",L"警报钟", L"警報鐘",L"警示钟", L"警示鐘",L"警钟", L"警鐘",L"译注", L"譯註",L"护发", L"護髮",L"读后", L"讀後",L"变征", L"變徵",L"变丑", L"變醜",L"变脏", L"變髒",L"变髒", L"變髒",L"仇問", L"讎問",L"仇夷", L"讎夷",L"仇校", L"讎校",L"仇正", L"讎正",L"仇隙", L"讎隙",L"赞不绝口", L"讚不絕口",L"赞佩", L"讚佩",L"赞呗", L"讚唄",L"赞叹不已", L"讚嘆不已",L"赞扬", L"讚揚",L"赞乐", L"讚樂",L"赞歌", L"讚歌",L"赞叹", L"讚歎",L"赞美", L"讚美",L"赞羡", L"讚羨",L"赞许", L"讚許",L"赞词", L"讚詞",L"赞誉", L"讚譽",L"赞赏", L"讚賞",L"赞辞", L"讚辭",L"赞颂", L"讚頌",L"豆干", L"豆乾",L"豆腐干", L"豆腐乾",L"竖着", L"豎著",L"竖起脊梁", L"豎起脊梁",L"丰滨", L"豐濱",L"丰滨乡", L"豐濱鄉",L"象征", L"象徵",L"象征着", L"象徵著",L"负债累累", L"負債纍纍",L"贪欲", L"貪慾",L"贵价", L"貴价",L"贵干", L"貴幹",L"贵征", L"貴徵",L"買凶", L"買兇",L"买凶", L"買兇",L"买断发", L"買斷發",L"费占", L"費佔",L"贻范", L"貽範",L"资金占用", L"資金占用",L"贾后", L"賈後",L"赈饥", L"賑饑",L"赏赞", L"賞讚",L"卖断发", L"賣斷發",L"卖呆", L"賣獃",L"质朴", L"質樸",L"赌台", L"賭檯",L"赌斗", L"賭鬥",L"賸余", L"賸餘",L"购并", L"購併",L"购买欲", L"購買慾",L"赢余", L"贏餘",L"赤绳系足", L"赤繩繫足",L"赤霉素", L"赤霉素",L"走回路", L"走回路",L"走后", L"走後",L"起复", L"起複",L"起哄", L"起鬨",L"超级杯", L"超級盃",L"赶制", L"趕製",L"赶面棍", L"趕麵棍",L"赵治勋", L"趙治勳",L"赵庄", L"趙莊",L"趱干", L"趲幹",L"足于", L"足於",L"跌扑", L"跌扑",L"跌荡", L"跌蕩",L"跟前跟后", L"跟前跟後",L"路签", L"路籤",L"跳梁小丑", L"跳樑小丑",L"跳荡", L"跳蕩",L"跳表", L"跳錶",L"蹪于", L"蹪於",L"蹭棱子", L"蹭稜子",L"躁郁", L"躁鬱",L"身后", L"身後",L"身于", L"身於",L"身体发肤", L"身體髮膚",L"躯干", L"軀幹",L"车库里", L"車庫裡",L"车站里", L"車站裡",L"车里", L"車裡",L"轨范", L"軌範",L"军队克制", L"軍隊剋制",L"轩辟", L"軒闢",L"较于", L"較於",L"挽曲", L"輓曲",L"挽歌", L"輓歌",L"挽聯", L"輓聯",L"挽联", L"輓聯",L"挽詞", L"輓詞",L"挽词", L"輓詞",L"挽诗", L"輓詩",L"挽詩", L"輓詩",L"轻于", L"輕於",L"轻轻松松", L"輕輕鬆鬆",L"轻松", L"輕鬆",L"轮奸", L"輪姦",L"轮回", L"輪迴",L"转向往", L"轉向往",L"转台", L"轉檯",L"转托", L"轉託",L"转斗千里", L"轉鬥千里",L"辛丑", L"辛丑",L"辟谷", L"辟穀",L"办公台", L"辦公檯",L"辞汇", L"辭彙",L"辫发", L"辮髮",L"辩斗", L"辯鬥",L"农历", L"農曆",L"农历史", L"農歷史",L"农民历", L"農民曆",L"农民历史", L"農民歷史",L"农庄", L"農莊",L"农药", L"農藥",L"迂回", L"迂迴",L"近日無仇", L"近日無讎",L"近日里", L"近日裡",L"返朴", L"返樸",L"迥然回异", L"迥然迴異",L"迫于", L"迫於",L"回光返照", L"迴光返照",L"回向", L"迴向",L"回圈", L"迴圈",L"回廊", L"迴廊",L"回形夹", L"迴形夾",L"回文", L"迴文",L"回旋", L"迴旋",L"回流", L"迴流",L"回环", L"迴環",L"回纹针", L"迴紋針",L"回绕", L"迴繞",L"回翔", L"迴翔",L"回肠", L"迴腸",L"回诵", L"迴誦",L"回路", L"迴路",L"回转", L"迴轉",L"回递性", L"迴遞性",L"回避", L"迴避",L"回銮", L"迴鑾",L"回音", L"迴音",L"回响", L"迴響",L"回风", L"迴風",L"迷幻药", L"迷幻藥",L"迷于", L"迷於",L"迷蒙", L"迷濛",L"迷药", L"迷藥",L"迷魂药", L"迷魂藥",L"追凶", L"追兇",L"退伙", L"退夥",L"退后", L"退後",L"退烧药", L"退燒藥",L"退藏于密", L"退藏於密",L"逆钟", L"逆鐘",L"逆钟向", L"逆鐘向",L"逋发", L"逋髮",L"逍遥游", L"逍遙遊",L"透辟", L"透闢",L"这只是", L"這只是",L"这伙人", L"這夥人",L"这里", L"這裡",L"这钟", L"這鐘",L"这只", L"這隻",L"这么", L"這麼",L"这么着", L"這麼著",L"通奸", L"通姦",L"通心面", L"通心麵",L"通于", L"通於",L"通历", L"通曆",L"通历史", L"通歷史",L"通庄", L"通莊",L"逞凶鬥狠", L"逞兇鬥狠",L"逞凶斗狠", L"逞兇鬥狠",L"造钟", L"造鐘",L"造钟表", L"造鐘錶",L"造曲", L"造麯",L"连三并四", L"連三併四",L"连占", L"連佔",L"连采", L"連採",L"连系", L"連繫",L"连庄", L"連莊",L"周游世界", L"週遊世界",L"进占", L"進佔",L"逼并", L"逼併",L"游了", L"遊了",L"游人", L"遊人",L"游仙", L"遊仙",L"游伴", L"遊伴",L"游侠", L"遊俠",L"游冶", L"遊冶",L"游刃有余", L"遊刃有餘",L"游动", L"遊動",L"游园", L"遊園",L"游子", L"遊子",L"游学", L"遊學",L"游客", L"遊客",L"游宦", L"遊宦",L"游山玩水", L"遊山玩水",L"游必有方", L"遊必有方",L"游憩", L"遊憩",L"游戏", L"遊戲",L"游戏里", L"遊戲裡",L"游手好闲", L"遊手好閒",L"游方", L"遊方",L"游星", L"遊星",L"游乐", L"遊樂",L"游标卡尺", L"遊標卡尺",L"游历", L"遊歷",L"游民", L"遊民",L"游河", L"遊河",L"游猎", L"遊獵",L"游玩", L"遊玩",L"游荡", L"遊盪",L"游目骋怀", L"遊目騁懷",L"游程", L"遊程",L"游丝", L"遊絲",L"游兴", L"遊興",L"游船", L"遊船",L"游艇", L"遊艇",L"游荡不归", L"遊蕩不歸",L"游艺", L"遊藝",L"游行", L"遊行",L"游街", L"遊街",L"游览", L"遊覽",L"游记", L"遊記",L"游说", L"遊說",L"游资", L"遊資",L"游走", L"遊走",L"游踪", L"遊蹤",L"游逛", L"遊逛",L"游错", L"遊錯",L"游离", L"遊離",L"游骑兵", L"遊騎兵",L"游魂", L"遊魂",L"过后", L"過後",L"过于", L"過於",L"过杆", L"過杆",L"过水面", L"過水麵",L"道范", L"道範",L"逊于", L"遜於",L"递回", L"遞迴",L"远县才至", L"遠縣纔至",L"远游", L"遠遊",L"遨游", L"遨遊",L"遮丑", L"遮醜",L"迁于", L"遷於",L"选手表明", L"選手表明",L"选手表决", L"選手表決",L"选手表现", L"選手表現",L"选手表示", L"選手表示",L"选手表达", L"選手表達",L"遗传钟", L"遺傳鐘",L"遗范", L"遺範",L"遗迹", L"遺迹",L"辽沈", L"遼瀋",L"避孕药", L"避孕藥",L"邀天之幸", L"邀天之倖",L"还占", L"還佔",L"还采", L"還採",L"还冲", L"還衝",L"邋里邋遢", L"邋裡邋遢",L"那只是", L"那只是",L"那只有", L"那只有",L"那卷", L"那捲",L"那里", L"那裡",L"那只", L"那隻",L"那么", L"那麼",L"那么着", L"那麼著",L"郁朴", L"郁樸",L"郁郁菲菲", L"郁郁菲菲",L"郊游", L"郊遊",L"郘钟", L"郘鐘",L"部落发", L"部落發",L"都于", L"都於",L"乡愿", L"鄉愿",L"邓后", L"鄧後",L"鄭凱云", L"鄭凱云",L"郑凯云", L"鄭凱云",L"郑庄公", L"鄭莊公",L"配制饲料", L"配制飼料",L"配合着", L"配合著",L"配水干管", L"配水幹管",L"配药", L"配藥",L"配制", L"配製",L"酒帘", L"酒帘",L"酒后", L"酒後",L"酒坛", L"酒罈",L"酒肴", L"酒肴",L"酒药", L"酒藥",L"酒醴曲蘖", L"酒醴麴櫱",L"酒曲", L"酒麴",L"酥松", L"酥鬆",L"醇朴", L"醇樸",L"醉于", L"醉於",L"醋坛", L"醋罈",L"丑丫头", L"醜丫頭",L"丑事", L"醜事",L"丑人", L"醜人",L"丑侪", L"醜儕",L"丑八怪", L"醜八怪",L"丑剌剌", L"醜剌剌",L"丑剧", L"醜劇",L"丑化", L"醜化",L"丑史", L"醜史",L"丑名", L"醜名",L"丑咤", L"醜吒",L"丑地", L"醜地",L"丑夷", L"醜夷",L"丑女", L"醜女",L"丑女效颦", L"醜女效顰",L"丑奴儿", L"醜奴兒",L"丑妇", L"醜婦",L"丑媳", L"醜媳",L"丑媳妇", L"醜媳婦",L"丑小鸭", L"醜小鴨",L"丑巴怪", L"醜巴怪",L"丑徒", L"醜徒",L"丑恶", L"醜惡",L"丑态", L"醜態",L"丑毙了", L"醜斃了",L"丑于", L"醜於",L"丑末", L"醜末",L"丑样", L"醜樣",L"丑死", L"醜死",L"丑比", L"醜比",L"丑沮", L"醜沮",L"丑男", L"醜男",L"丑闻", L"醜聞",L"丑声", L"醜聲",L"丑声远播", L"醜聲遠播",L"丑脸", L"醜臉",L"丑虏", L"醜虜",L"丑行", L"醜行",L"丑言", L"醜言",L"丑诋", L"醜詆",L"丑话", L"醜話",L"丑语", L"醜語",L"丑贼生", L"醜賊生",L"丑辞", L"醜辭",L"丑辱", L"醜辱",L"丑逆", L"醜逆",L"丑丑", L"醜醜",L"丑陋", L"醜陋",L"丑杂", L"醜雜",L"丑头怪脸", L"醜頭怪臉",L"丑类", L"醜類",L"酝酿着", L"醞釀著",L"医药", L"醫藥",L"医院里", L"醫院裡",L"酿制", L"釀製",L"衅钟", L"釁鐘",L"采石之役", L"采石之役",L"采石之战", L"采石之戰",L"采石之戰", L"采石之戰",L"采石磯", L"采石磯",L"采石矶", L"采石磯",L"釉药", L"釉藥",L"里程表", L"里程錶",L"重划", L"重劃",L"重回", L"重回",L"重折", L"重摺",L"重于", L"重於",L"重罗面", L"重羅麵",L"重制", L"重製",L"重复", L"重複",L"重托", L"重託",L"重游", L"重遊",L"重锤", L"重鎚",L"野姜", L"野薑",L"野游", L"野遊",L"厘出", L"釐出",L"厘升", L"釐升",L"厘定", L"釐定",L"厘正", L"釐正",L"厘清", L"釐清",L"厘订", L"釐訂",L"金仆姑", L"金僕姑",L"金仑溪", L"金崙溪",L"金布道", L"金布道",L"金范", L"金範",L"金表情", L"金表情",L"金表态", L"金表態",L"金表扬", L"金表揚",L"金表明", L"金表明",L"金表演", L"金表演",L"金表现", L"金表現",L"金表示", L"金表示",L"金表达", L"金表達",L"金表露", L"金表露",L"金表面", L"金表面",L"金装玉里", L"金裝玉裡",L"金表", L"金錶",L"金钟", L"金鐘",L"金马仑道", L"金馬崙道",L"金发", L"金髮",L"钉锤", L"釘鎚",L"钩心斗角", L"鈎心鬥角",L"铃响后", L"鈴響後",L"银朱", L"銀硃",L"银发", L"銀髮",L"铜范", L"銅範",L"铜制", L"銅製",L"铜钟", L"銅鐘",L"铯钟", L"銫鐘",L"铝制", L"鋁製",L"铺锦列绣", L"鋪錦列繡",L"钢之炼金术师", L"鋼之鍊金術師",L"钢梁", L"鋼樑",L"钢制", L"鋼製",L"录着", L"錄著",L"录制", L"錄製",L"锤炼", L"錘鍊",L"钱谷", L"錢穀",L"钱范", L"錢範",L"钱庄", L"錢莊",L"锦绣花园", L"錦綉花園",L"锦绣", L"錦繡",L"表停", L"錶停",L"表冠", L"錶冠",L"表带", L"錶帶",L"表店", L"錶店",L"表厂", L"錶廠",L"表快", L"錶快",L"表慢", L"錶慢",L"表板", L"錶板",L"表壳", L"錶殼",L"表王", L"錶王",L"表的嘀嗒", L"錶的嘀嗒",L"表的历史", L"錶的歷史",L"表盘", L"錶盤",L"表蒙子", L"錶蒙子",L"表行", L"錶行",L"表转", L"錶轉",L"表速", L"錶速",L"表针", L"錶針",L"表链", L"錶鏈",L"炼冶", L"鍊冶",L"炼句", L"鍊句",L"炼字", L"鍊字",L"炼师", L"鍊師",L"炼度", L"鍊度",L"炼形", L"鍊形",L"炼气", L"鍊氣",L"炼汞", L"鍊汞",L"炼石", L"鍊石",L"炼贫", L"鍊貧",L"炼金术", L"鍊金術",L"炼钢", L"鍊鋼",L"锅庄", L"鍋莊",L"锻炼出", L"鍛鍊出",L"锲而不舍", L"鍥而不捨",L"镰仓", L"鎌倉",L"锤儿", L"鎚兒",L"锤子", L"鎚子",L"锤头", L"鎚頭",L"锈病", L"鏽病",L"锈菌", L"鏽菌",L"锈蚀", L"鏽蝕",L"钟上", L"鐘上",L"钟下", L"鐘下",L"钟不", L"鐘不",L"钟不扣不鸣", L"鐘不扣不鳴",L"钟不撞不鸣", L"鐘不撞不鳴",L"钟不敲不响", L"鐘不敲不響",L"钟不空则哑", L"鐘不空則啞",L"钟乳洞", L"鐘乳洞",L"钟乳石", L"鐘乳石",L"钟停", L"鐘停",L"钟匠", L"鐘匠",L"钟口", L"鐘口",L"钟在寺里", L"鐘在寺裡",L"钟塔", L"鐘塔",L"钟壁", L"鐘壁",L"钟太", L"鐘太",L"钟好", L"鐘好",L"钟山", L"鐘山",L"钟左右", L"鐘左右",L"钟差", L"鐘差",L"钟座", L"鐘座",L"钟形", L"鐘形",L"钟形虫", L"鐘形蟲",L"钟律", L"鐘律",L"钟快", L"鐘快",L"钟意", L"鐘意",L"钟慢", L"鐘慢",L"钟摆", L"鐘擺",L"钟敲", L"鐘敲",L"钟有", L"鐘有",L"钟楼", L"鐘樓",L"钟模", L"鐘模",L"钟没", L"鐘沒",L"钟漏", L"鐘漏",L"钟王", L"鐘王",L"钟琴", L"鐘琴",L"钟发音", L"鐘發音",L"钟的", L"鐘的",L"钟盘", L"鐘盤",L"钟相", L"鐘相",L"钟磬", L"鐘磬",L"钟纽", L"鐘紐",L"钟罩", L"鐘罩",L"钟声", L"鐘聲",L"钟腰", L"鐘腰",L"钟螺", L"鐘螺",L"钟行", L"鐘行",L"钟表面", L"鐘表面",L"钟被", L"鐘被",L"钟调", L"鐘調",L"钟身", L"鐘身",L"钟速", L"鐘速",L"钟表", L"鐘錶",L"钟表停", L"鐘錶停",L"钟表快", L"鐘錶快",L"钟表慢", L"鐘錶慢",L"钟表历史", L"鐘錶歷史",L"钟表王", L"鐘錶王",L"钟表的", L"鐘錶的",L"钟表的历史", L"鐘錶的歷史",L"钟表盘", L"鐘錶盤",L"钟表行", L"鐘錶行",L"钟表速", L"鐘錶速",L"钟关", L"鐘關",L"钟陈列", L"鐘陳列",L"钟面", L"鐘面",L"钟响", L"鐘響",L"钟顶", L"鐘頂",L"钟头", L"鐘頭",L"钟体", L"鐘體",L"钟鸣", L"鐘鳴",L"钟点", L"鐘點",L"钟鼎", L"鐘鼎",L"钟鼓", L"鐘鼓",L"铁杆", L"鐵杆",L"铁栏杆", L"鐵欄杆",L"铁锤", L"鐵鎚",L"铁锈", L"鐵鏽",L"铁钟", L"鐵鐘",L"铸钟", L"鑄鐘",L"鉴于", L"鑒於",L"长几", L"長几",L"长于", L"長於",L"长历", L"長曆",L"长历史", L"長歷史",L"长生药", L"長生藥",L"长胡", L"長鬍",L"门前门后", L"門前門後",L"门帘", L"門帘",L"门吊儿", L"門弔兒",L"门里", L"門裡",L"闫怀礼", L"閆懷禮",L"开吊", L"開弔",L"开征", L"開徵",L"开采", L"開採",L"开发", L"開發",L"开药", L"開藥",L"开辟", L"開闢",L"开哄", L"開鬨",L"闲情逸致", L"閒情逸緻",L"闲荡", L"閒蕩",L"闲游", L"閒遊",L"间不容发", L"間不容髮",L"闵采尔", L"閔採爾",L"合府", L"閤府",L"闺范", L"閨範",L"阃范", L"閫範",L"闯荡", L"闖蕩",L"闯炼", L"闖鍊",L"关系", L"關係",L"关系着", L"關係著",L"关弓与我确", L"關弓與我确",L"关于", L"關於",L"辟佛", L"闢佛",L"辟作", L"闢作",L"辟划", L"闢劃",L"辟土", L"闢土",L"辟地", L"闢地",L"辟室", L"闢室",L"辟建", L"闢建",L"辟为", L"闢為",L"辟田", L"闢田",L"辟筑", L"闢築",L"辟谣", L"闢謠",L"辟辟", L"闢辟",L"辟邪以律", L"闢邪以律",L"防晒", L"防晒",L"防水表", L"防水錶",L"防御", L"防禦",L"防范", L"防範",L"防锈", L"防鏽",L"防台", L"防颱",L"阻于", L"阻於",L"阿呆瓜", L"阿呆瓜",L"阿斯图里亚斯", L"阿斯圖里亞斯",L"阿呆", L"阿獃",L"附于", L"附於",L"附注", L"附註",L"降压药", L"降壓藥",L"限制", L"限制",L"升官", L"陞官",L"除臭药", L"除臭藥",L"陪吊", L"陪弔",L"阴干", L"陰乾",L"阴历", L"陰曆",L"阴历史", L"陰歷史",L"阴沟里翻船", L"陰溝裡翻船",L"阴郁", L"陰鬱",L"陈炼", L"陳鍊",L"陆游", L"陸遊",L"阳春面", L"陽春麵",L"阳历", L"陽曆",L"阳历史", L"陽歷史",L"隆准许", L"隆准許",L"隆准", L"隆準",L"随后", L"隨後",L"随于", L"隨於",L"隐占", L"隱佔",L"隐几", L"隱几",L"隐于", L"隱於",L"只字", L"隻字",L"只影", L"隻影",L"只手遮天", L"隻手遮天",L"只眼", L"隻眼",L"只言片语", L"隻言片語",L"只身", L"隻身",L"雄斗斗", L"雄斗斗",L"雅范", L"雅範",L"雅致", L"雅緻",L"集于", L"集於",L"集游法", L"集遊法",L"雇佣", L"雇傭",L"雕梁画栋", L"雕樑畫棟",L"双折射", L"雙折射",L"双折", L"雙摺",L"双胜类", L"雙胜類",L"双雕", L"雙鵰",L"杂合面儿", L"雜合麵兒",L"杂志", L"雜誌",L"杂面", L"雜麵",L"鸡吵鹅斗", L"雞吵鵝鬥",L"鸡奸", L"雞姦",L"鸡争鹅斗", L"雞爭鵝鬥",L"鸡丝", L"雞絲",L"鸡丝面", L"雞絲麵",L"鸡腿面", L"雞腿麵",L"鸡蛋里挑骨头", L"雞蛋裡挑骨頭",L"鸡只", L"雞隻",L"离于", L"離於",L"难舍", L"難捨",L"难于", L"難於",L"雨后", L"雨後",L"雪窗萤几", L"雪窗螢几",L"雪里", L"雪裡",L"雪里红", L"雪裡紅",L"雪里蕻", L"雪裡蕻",L"云南白药", L"雲南白藥",L"云笈七签", L"雲笈七籤",L"云游", L"雲遊",L"云须", L"雲鬚",L"零个", L"零個",L"零多只", L"零多隻",L"零天后", L"零天後",L"零只", L"零隻",L"零余", L"零餘",L"电子表格", L"電子表格",L"电子表", L"電子錶",L"电子钟", L"電子鐘",L"电子钟表", L"電子鐘錶",L"电杆", L"電杆",L"电码表", L"電碼表",L"电线杆", L"電線杆",L"电冲", L"電衝",L"电表", L"電錶",L"电钟", L"電鐘",L"震栗", L"震慄",L"震荡", L"震蕩",L"雾里", L"霧裡",L"露丑", L"露醜",L"霸占", L"霸佔",L"霁范", L"霽範",L"灵药", L"靈藥",L"青山一发", L"青山一髮",L"青苹", L"青苹",L"青苹果", L"青蘋果",L"青蝇吊客", L"青蠅弔客",L"青霉素", L"青霉素",L"青霉", L"青黴",L"非占不可", L"非佔不可",L"靠后", L"靠後",L"面包住", L"面包住",L"面包含", L"面包含",L"面包围", L"面包圍",L"面包容", L"面包容",L"面包庇", L"面包庇",L"面包厢", L"面包廂",L"面包抄", L"面包抄",L"面包括", L"面包括",L"面包揽", L"面包攬",L"面包涵", L"面包涵",L"面包管", L"面包管",L"面包扎", L"面包紮",L"面包罗", L"面包羅",L"面包着", L"面包著",L"面包藏", L"面包藏",L"面包装", L"面包裝",L"面包裹", L"面包裹",L"面包起", L"面包起",L"面包办", L"面包辦",L"面店舖", L"面店舖",L"面朝着", L"面朝著",L"面条目", L"面條目",L"面條目", L"面條目",L"面粉碎", L"面粉碎",L"面粉红", L"面粉紅",L"面临着", L"面臨著",L"面食饭", L"面食飯",L"面食面", L"面食麵",L"鞋里", L"鞋裡",L"鞣制", L"鞣製",L"秋千", L"鞦韆",L"鞭辟入里", L"鞭辟入裡",L"韦庄", L"韋莊",L"韩国制", L"韓國製",L"韩制", L"韓製",L"音准", L"音準",L"音声如钟", L"音聲如鐘",L"韶山冲", L"韶山沖",L"响钟", L"響鐘",L"頁面", L"頁面",L"页面", L"頁面",L"頂多", L"頂多",L"顶多", L"頂多",L"项庄", L"項莊",L"顺于", L"順於",L"顺钟向", L"順鐘向",L"须根据", L"須根據",L"颂系", L"頌繫",L"颂赞", L"頌讚",L"预制", L"預製",L"领域里", L"領域裡",L"领袖欲", L"領袖慾",L"头巾吊在水里", L"頭巾弔在水裡",L"头里", L"頭裡",L"头发", L"頭髮",L"颊须", L"頰鬚",L"题签", L"題籤",L"额征", L"額徵",L"额我略历", L"額我略曆",L"额我略历史", L"額我略歷史",L"颜范", L"顏範",L"颠干倒坤", L"顛乾倒坤",L"颠覆", L"顛覆",L"颠颠仆仆", L"顛顛仆仆",L"顾前不顾后", L"顧前不顧後",L"颤栗", L"顫慄",L"显示表", L"顯示錶",L"显示钟", L"顯示鐘",L"显示钟表", L"顯示鐘錶",L"显著标志", L"顯著標志",L"风干", L"風乾",L"风土志", L"風土誌",L"风卷残云", L"風捲殘雲",L"风物志", L"風物誌",L"风范", L"風範",L"风里", L"風裡",L"风起云涌", L"風起雲湧",L"风采", L"風采",L"風采", L"風采",L"台风", L"颱風",L"刮了", L"颳了",L"刮倒", L"颳倒",L"刮去", L"颳去",L"刮得", L"颳得",L"刮走", L"颳走",L"刮起", L"颳起",L"刮雪", L"颳雪",L"刮风", L"颳風",L"飘荡", L"飄蕩",L"飘游", L"飄遊",L"飘飘荡荡", L"飄飄蕩蕩",L"飞扎", L"飛紮",L"飞刍挽粟", L"飛芻輓粟",L"飞行钟", L"飛行鐘",L"食欲", L"食慾",L"食欲不振", L"食欲不振",L"食野之苹", L"食野之苹",L"食面", L"食麵",L"饭后", L"飯後",L"饭后钟", L"飯後鐘",L"饭团", L"飯糰",L"饭庄", L"飯莊",L"饲喂", L"飼餵",L"饼干", L"餅乾",L"馂余", L"餕餘",L"余0", L"餘0",L"余1", L"餘1",L"余2", L"餘2",L"余3", L"餘3",L"余4", L"餘4",L"余5", L"餘5",L"余6", L"餘6",L"余7", L"餘7",L"余8", L"餘8",L"余9", L"餘9",L"余〇", L"餘〇",L"余一", L"餘一",L"余七", L"餘七",L"余三", L"餘三",L"余下", L"餘下",L"余九", L"餘九",L"余事", L"餘事",L"余二", L"餘二",L"余五", L"餘五",L"余人", L"餘人",L"余俗", L"餘俗",L"余倍", L"餘倍",L"余僇", L"餘僇",L"余光", L"餘光",L"余八", L"餘八",L"余六", L"餘六",L"余刃", L"餘刃",L"余切", L"餘切",L"余利", L"餘利",L"余割", L"餘割",L"余力", L"餘力",L"余勇", L"餘勇",L"余十", L"餘十",L"余味", L"餘味",L"余喘", L"餘喘",L"余四", L"餘四",L"余地", L"餘地",L"余墨", L"餘墨",L"余外", L"餘外",L"余妙", L"餘妙",L"余姚", L"餘姚",L"余威", L"餘威",L"余子", L"餘子",L"余存", L"餘存",L"余孽", L"餘孽",L"余弦", L"餘弦",L"余思", L"餘思",L"余悸", L"餘悸",L"余庆", L"餘慶",L"余数", L"餘數",L"余明", L"餘明",L"余映", L"餘映",L"余暇", L"餘暇",L"余晖", L"餘暉",L"余杭", L"餘杭",L"余杯", L"餘杯",L"余桃", L"餘桃",L"余桶", L"餘桶",L"余业", L"餘業",L"余款", L"餘款",L"余步", L"餘步",L"余殃", L"餘殃",L"余毒", L"餘毒",L"余气", L"餘氣",L"余波", L"餘波",L"余波荡漾", L"餘波盪漾",L"余温", L"餘溫",L"余泽", L"餘澤",L"余沥", L"餘瀝",L"余烈", L"餘烈",L"余热", L"餘熱",L"余烬", L"餘燼",L"余珍", L"餘珍",L"余生", L"餘生",L"余众", L"餘眾",L"余窍", L"餘竅",L"余粮", L"餘糧",L"余绪", L"餘緒",L"余缺", L"餘缺",L"余罪", L"餘罪",L"余羡", L"餘羨",L"余声", L"餘聲",L"余膏", L"餘膏",L"余兴", L"餘興",L"余蓄", L"餘蓄",L"余荫", L"餘蔭",L"余裕", L"餘裕",L"余角", L"餘角",L"余论", L"餘論",L"余责", L"餘責",L"余貾", L"餘貾",L"余辉", L"餘輝",L"余辜", L"餘辜",L"余酲", L"餘酲",L"余闰", L"餘閏",L"余闲", L"餘閒",L"余零", L"餘零",L"余震", L"餘震",L"余霞", L"餘霞",L"余音", L"餘音",L"余音绕梁", L"餘音繞梁",L"余韵", L"餘韻",L"余响", L"餘響",L"余额", L"餘額",L"余风", L"餘風",L"余食", L"餘食",L"余党", L"餘黨",L"余０", L"餘０",L"余１", L"餘１",L"余２", L"餘２",L"余３", L"餘３",L"余４", L"餘４",L"余５", L"餘５",L"余６", L"餘６",L"余７", L"餘７",L"余８", L"餘８",L"余９", L"餘９",L"馄饨面", L"餛飩麵",L"馆后一街", L"館後一街",L"馆后二街", L"館後二街",L"馆谷", L"館穀",L"喂乳", L"餵乳",L"喂了", L"餵了",L"喂奶", L"餵奶",L"喂给", L"餵給",L"喂羊", L"餵羊",L"喂猪", L"餵豬",L"喂过", L"餵過",L"喂鸡", L"餵雞",L"喂食", L"餵食",L"喂饱", L"餵飽",L"喂养", L"餵養",L"喂驴", L"餵驢",L"喂鱼", L"餵魚",L"喂鸭", L"餵鴨",L"喂鹅", L"餵鵝",L"饥寒", L"饑寒",L"饥民", L"饑民",L"饥渴", L"饑渴",L"饥溺", L"饑溺",L"饥荒", L"饑荒",L"饥饱", L"饑飽",L"饥馑", L"饑饉",L"首当其冲", L"首當其衝",L"首发", L"首發",L"首只", L"首隻",L"香干", L"香乾",L"香山庄", L"香山庄",L"马干", L"馬乾",L"马占山", L"馬占山",L"馬占山", L"馬占山",L"马后", L"馬後",L"马杆", L"馬杆",L"马表", L"馬錶",L"驻扎", L"駐紮",L"骀荡", L"駘蕩",L"腾冲", L"騰衝",L"惊赞", L"驚讚",L"惊钟", L"驚鐘",L"骨子里", L"骨子裡",L"骨干", L"骨幹",L"骨灰坛", L"骨灰罈",L"骨坛", L"骨罈",L"骨头里挣出来的钱才做得肉", L"骨頭裡掙出來的錢纔做得肉",L"肮肮脏脏", L"骯骯髒髒",L"肮脏", L"骯髒",L"脏乱", L"髒亂",L"脏了", L"髒了",L"脏兮兮", L"髒兮兮",L"脏字", L"髒字",L"脏得", L"髒得",L"脏心", L"髒心",L"脏东西", L"髒東西",L"脏水", L"髒水",L"脏的", L"髒的",L"脏词", L"髒詞",L"脏话", L"髒話",L"脏钱", L"髒錢",L"脏发", L"髒髮",L"体范", L"體範",L"体系", L"體系",L"高几", L"高几",L"高干扰", L"高干擾",L"高干预", L"高干預",L"高干", L"高幹",L"高度自制", L"高度自制",L"髡发", L"髡髮",L"髭胡", L"髭鬍",L"髭须", L"髭鬚",L"发上指冠", L"髮上指冠",L"发上冲冠", L"髮上沖冠",L"发乳", L"髮乳",L"发光可鉴", L"髮光可鑑",L"发匪", L"髮匪",L"发型", L"髮型",L"发夹", L"髮夾",L"发妻", L"髮妻",L"发姐", L"髮姐",L"发屋", L"髮屋",L"发已霜白", L"髮已霜白",L"发带", L"髮帶",L"发廊", L"髮廊",L"发式", L"髮式",L"发引千钧", L"髮引千鈞",L"发指", L"髮指",L"发卷", L"髮捲",L"发根", L"髮根",L"发油", L"髮油",L"发漂", L"髮漂",L"发为血之本", L"髮為血之本",L"发状", L"髮狀",L"发癣", L"髮癬",L"发短心长", L"髮短心長",L"发禁", L"髮禁",L"发笺", L"髮箋",L"发纱", L"髮紗",L"发结", L"髮結",L"发丝", L"髮絲",L"发网", L"髮網",L"发脚", L"髮腳",L"发肤", L"髮膚",L"发胶", L"髮膠",L"发菜", L"髮菜",L"发蜡", L"髮蠟",L"发踊冲冠", L"髮踊沖冠",L"发辫", L"髮辮",L"发针", L"髮針",L"发钗", L"髮釵",L"发长", L"髮長",L"发际", L"髮際",L"发雕", L"髮雕",L"发霜", L"髮霜",L"发饰", L"髮飾",L"发髻", L"髮髻",L"发鬓", L"髮鬢",L"髯胡", L"髯鬍",L"髼松", L"髼鬆",L"鬅松", L"鬅鬆",L"松一口气", L"鬆一口氣",L"松了", L"鬆了",L"松些", L"鬆些",L"松元音", L"鬆元音",L"松劲", L"鬆勁",L"松动", L"鬆動",L"松口", L"鬆口",L"松喉", L"鬆喉",L"松土", L"鬆土",L"松宽", L"鬆寬",L"松弛", L"鬆弛",L"松快", L"鬆快",L"松懈", L"鬆懈",L"松手", L"鬆手",L"松掉", L"鬆掉",L"松散", L"鬆散",L"松柔", L"鬆柔",L"松气", L"鬆氣",L"松浮", L"鬆浮",L"松绑", L"鬆綁",L"松紧", L"鬆緊",L"松缓", L"鬆緩",L"松脆", L"鬆脆",L"松脱", L"鬆脫",L"松蛋", L"鬆蛋",L"松起", L"鬆起",L"松软", L"鬆軟",L"松通", L"鬆通",L"松开", L"鬆開",L"松饼", L"鬆餅",L"松松", L"鬆鬆",L"鬈发", L"鬈髮",L"胡子", L"鬍子",L"胡梢", L"鬍梢",L"胡渣", L"鬍渣",L"胡髭", L"鬍髭",L"胡髯", L"鬍髯",L"胡须", L"鬍鬚",L"鬒发", L"鬒髮",L"须根", L"鬚根",L"须毛", L"鬚毛",L"须生", L"鬚生",L"须眉", L"鬚眉",L"须发", L"鬚髮",L"须胡", L"鬚鬍",L"须须", L"鬚鬚",L"须鲨", L"鬚鯊",L"须鲸", L"鬚鯨",L"鬓发", L"鬢髮",L"斗上", L"鬥上",L"斗不过", L"鬥不過",L"斗了", L"鬥了",L"斗来斗去", L"鬥來鬥去",L"斗倒", L"鬥倒",L"斗分子", L"鬥分子",L"斗力", L"鬥力",L"斗劲", L"鬥勁",L"斗胜", L"鬥勝",L"斗口", L"鬥口",L"斗合", L"鬥合",L"斗嘴", L"鬥嘴",L"斗地主", L"鬥地主",L"斗士", L"鬥士",L"斗富", L"鬥富",L"斗巧", L"鬥巧",L"斗幌子", L"鬥幌子",L"斗弄", L"鬥弄",L"斗引", L"鬥引",L"斗别气", L"鬥彆氣",L"斗彩", L"鬥彩",L"斗心眼", L"鬥心眼",L"斗志", L"鬥志",L"斗闷", L"鬥悶",L"斗成", L"鬥成",L"斗打", L"鬥打",L"斗批改", L"鬥批改",L"斗技", L"鬥技",L"斗文", L"鬥文",L"斗智", L"鬥智",L"斗暴", L"鬥暴",L"斗武", L"鬥武",L"斗殴", L"鬥毆",L"斗气", L"鬥氣",L"斗法", L"鬥法",L"斗争", L"鬥爭",L"斗争斗合", L"鬥爭鬥合",L"斗牌", L"鬥牌",L"斗牙拌齿", L"鬥牙拌齒",L"斗牙斗齿", L"鬥牙鬥齒",L"斗牛", L"鬥牛",L"斗犀台", L"鬥犀臺",L"斗犬", L"鬥犬",L"斗狠", L"鬥狠",L"斗叠", L"鬥疊",L"斗百草", L"鬥百草",L"斗眼", L"鬥眼",L"斗私批修", L"鬥私批修",L"斗而铸兵", L"鬥而鑄兵",L"斗而铸锥", L"鬥而鑄錐",L"斗脚", L"鬥腳",L"斗舰", L"鬥艦",L"斗茶", L"鬥茶",L"斗草", L"鬥草",L"斗叶儿", L"鬥葉兒",L"斗叶子", L"鬥葉子",L"斗着", L"鬥著",L"斗蟋蟀", L"鬥蟋蟀",L"斗话", L"鬥話",L"斗艳", L"鬥豔",L"斗起", L"鬥起",L"斗趣", L"鬥趣",L"斗闲气", L"鬥閑氣",L"斗鸡", L"鬥雞",L"斗雪红", L"鬥雪紅",L"斗头", L"鬥頭",L"斗风", L"鬥風",L"斗饤", L"鬥飣",L"斗斗", L"鬥鬥",L"斗哄", L"鬥鬨",L"斗鱼", L"鬥魚",L"斗鸭", L"鬥鴨",L"斗鹌鹑", L"鬥鵪鶉",L"斗丽", L"鬥麗",L"闹着玩儿", L"鬧著玩兒",L"闹表", L"鬧錶",L"闹钟", L"鬧鐘",L"哄动", L"鬨動",L"哄堂", L"鬨堂",L"哄笑", L"鬨笑",L"郁伊", L"鬱伊",L"郁勃", L"鬱勃",L"郁卒", L"鬱卒",L"郁南", L"鬱南",L"郁堙不偶", L"鬱堙不偶",L"郁塞", L"鬱塞",L"郁垒", L"鬱壘",L"郁律", L"鬱律",L"郁悒", L"鬱悒",L"郁闷", L"鬱悶",L"郁愤", L"鬱憤",L"郁抑", L"鬱抑",L"郁挹", L"鬱挹",L"郁林", L"鬱林",L"郁气", L"鬱氣",L"郁江", L"鬱江",L"郁沉沉", L"鬱沉沉",L"郁泱", L"鬱泱",L"郁火", L"鬱火",L"郁热", L"鬱熱",L"郁燠", L"鬱燠",L"郁症", L"鬱症",L"郁积", L"鬱積",L"郁纡", L"鬱紆",L"郁结", L"鬱結",L"郁蒸", L"鬱蒸",L"郁蓊", L"鬱蓊",L"郁血", L"鬱血",L"郁邑", L"鬱邑",L"郁郁", L"鬱郁",L"郁金", L"鬱金",L"郁闭", L"鬱閉",L"郁陶", L"鬱陶",L"郁郁不平", L"鬱鬱不平",L"郁郁不乐", L"鬱鬱不樂",L"郁郁寡欢", L"鬱鬱寡歡",L"郁郁而终", L"鬱鬱而終",L"郁郁葱葱", L"鬱鬱蔥蔥",L"郁黑", L"鬱黑",L"鬼谷子", L"鬼谷子",L"魂牵梦系", L"魂牽夢繫",L"魏征", L"魏徵",L"魔表", L"魔錶",L"鱼干", L"魚乾",L"鱼松", L"魚鬆",L"鲸须", L"鯨鬚",L"鲇鱼", L"鯰魚",L"鸠占鹊巢", L"鳩佔鵲巢",L"凤凰于飞", L"鳳凰于飛",L"凤梨干", L"鳳梨乾",L"鸣钟", L"鳴鐘",L"鸿案相庄", L"鴻案相莊",L"鸿范", L"鴻範",L"鸿篇巨制", L"鴻篇巨製",L"鹅准", L"鵝準",L"鹄发", L"鵠髮",L"雕心雁爪", L"鵰心雁爪",L"雕悍", L"鵰悍",L"雕翎", L"鵰翎",L"雕鹗", L"鵰鶚",L"鹤吊", L"鶴弔",L"鹤发", L"鶴髮",L"鹰雕", L"鹰鵰",L"咸味", L"鹹味",L"咸嘴淡舌", L"鹹嘴淡舌",L"咸土", L"鹹土",L"咸度", L"鹹度",L"咸得", L"鹹得",L"咸批", L"鹹批",L"咸水", L"鹹水",L"咸派", L"鹹派",L"咸海", L"鹹海",L"咸淡", L"鹹淡",L"咸湖", L"鹹湖",L"咸汤", L"鹹湯",L"咸潟", L"鹹潟",L"咸的", L"鹹的",L"咸粥", L"鹹粥",L"咸肉", L"鹹肉",L"咸菜", L"鹹菜",L"咸菜干", L"鹹菜乾",L"咸蛋", L"鹹蛋",L"咸猪肉", L"鹹豬肉",L"咸类", L"鹹類",L"咸食", L"鹹食",L"咸鱼", L"鹹魚",L"咸鸭蛋", L"鹹鴨蛋",L"咸卤", L"鹹鹵",L"咸咸", L"鹹鹹",L"盐打怎么咸", L"鹽打怎麼鹹",L"盐卤", L"鹽滷",L"盐余", L"鹽餘",L"丽于", L"麗於",L"曲尘", L"麴塵",L"曲蘖", L"麴櫱",L"曲生", L"麴生",L"曲秀才", L"麴秀才",L"曲菌", L"麴菌",L"曲车", L"麴車",L"曲道士", L"麴道士",L"曲钱", L"麴錢",L"曲院", L"麴院",L"曲霉", L"麴黴",L"面人儿", L"麵人兒",L"面价", L"麵價",L"面包", L"麵包",L"面坊", L"麵坊",L"面坯儿", L"麵坯兒",L"面塑", L"麵塑",L"面店", L"麵店",L"面厂", L"麵廠",L"面摊", L"麵攤",L"面杖", L"麵杖",L"面条", L"麵條",L"面汤", L"麵湯",L"面浆", L"麵漿",L"面灰", L"麵灰",L"面疙瘩", L"麵疙瘩",L"面皮", L"麵皮",L"面码儿", L"麵碼兒",L"面筋", L"麵筋",L"面粉", L"麵粉",L"面糊", L"麵糊",L"面团", L"麵糰",L"面线", L"麵線",L"面缸", L"麵缸",L"面茶", L"麵茶",L"面食", L"麵食",L"面饺", L"麵餃",L"面饼", L"麵餅",L"面馆", L"麵館",L"麻药", L"麻藥",L"麻醉药", L"麻醉藥",L"麻酱面", L"麻醬麵",L"黄干黑瘦", L"黃乾黑瘦",L"黄历", L"黃曆",L"黄曲霉", L"黃曲霉",L"黄历史", L"黃歷史",L"黄金表", L"黃金表",L"黃鈺筑", L"黃鈺筑",L"黄钰筑", L"黃鈺筑",L"黄钟", L"黃鐘",L"黄发", L"黃髮",L"黄曲毒素", L"黃麴毒素",L"黑奴吁天录", L"黑奴籲天錄",L"黑发", L"黑髮",L"点半钟", L"點半鐘",L"点多钟", L"點多鐘",L"点里", L"點裡",L"点钟", L"點鐘",L"霉毒", L"黴毒",L"霉素", L"黴素",L"霉菌", L"黴菌",L"霉黑", L"黴黑",L"霉黧", L"黴黧",L"鼓里", L"鼓裡",L"冬冬鼓", L"鼕鼕鼓",L"鼠药", L"鼠藥",L"鼠曲草", L"鼠麴草",L"鼻梁儿", L"鼻梁兒",L"鼻梁", L"鼻樑",L"鼻准", L"鼻準",L"齐王舍牛", L"齊王捨牛",L"齐庄", L"齊莊",L"齿危发秀", L"齒危髮秀",L"齿落发白", L"齒落髮白",L"齿发", L"齒髮",L"出儿", L"齣兒",L"出剧", L"齣劇",L"出动画", L"齣動畫",L"出卡通", L"齣卡通",L"出子", L"齣子",L"出戏", L"齣戲",L"出节目", L"齣節目",L"出电影", L"齣電影",L"出电视", L"齣電視",L"龙卷", L"龍捲",L"龙眼干", L"龍眼乾",L"龙须", L"龍鬚",L"龙斗虎伤", L"龍鬥虎傷",L"龟山庄", L"龜山庄",L"！克制", L"！剋制",L"，克制", L"，剋制",L"０多只", L"０多隻",L"０天后", L"０天後",L"０只", L"０隻",L"０余", L"０餘",L"１天后", L"１天後",L"１只", L"１隻",L"１余", L"１餘",L"２天后", L"２天後",L"２只", L"２隻",L"２余", L"２餘",L"３天后", L"３天後",L"３只", L"３隻",L"３余", L"３餘",L"４天后", L"４天後",L"４只", L"４隻",L"４余", L"４餘",L"５天后", L"５天後",L"５只", L"５隻",L"５余", L"５餘",L"６天后", L"６天後",L"６只", L"６隻",L"６余", L"６餘",L"７天后", L"７天後",L"７只", L"７隻",L"７余", L"７餘",L"８天后", L"８天後",L"８只", L"８隻",L"８余", L"８餘",L"９天后", L"９天後",L"９只", L"９隻",L"９余", L"９餘",L"：克制", L"：剋制",L"；克制", L"；剋制",L"？克制", L"？剋制",L"空中巴士", L"空中巴士",L"空中客车", L"空中巴士",L"空中客車", L"空中巴士",L"的士", L"的士",L"公车上书", L"公車上書",L"东加拿大", L"東加拿大",L"東加拿大", L"東加拿大",L"东加里曼丹", L"東加里曼丹",L"東加里曼丹", L"東加里曼丹",L"东加勒比元", L"東加勒比元",L"東加勒比元", L"東加勒比元",L"股东", L"股東",L"股東", L"股東",L"眼干", L"眼乾",L"后", L"後",L"艷后", L"艷后",L"艳后", L"艷后",L"廢后", L"廢后",L"废后", L"廢后",L"妖后", L"妖后",L"后髮座", L"后髮座",L"后海灣", L"后海灣",L"后海湾", L"后海灣",L"后发座", L"后髮座",L"仙后", L"仙后",L"贾后", L"賈后",L"贤后", L"賢后",L"賢后", L"賢后",L"賈后", L"賈后",L"蜂后", L"蜂后",L"皇后", L"皇后",L"王后", L"王后",L"王侯后", L"王侯后",L"母后", L"母后",L"武后", L"武后",L"歌后", L"歌后",L"影后", L"影后",L"封后", L"封后",L"太后", L"太后",L"天后", L"天后",L"呂后", L"呂后",L"吕后", L"呂后",L"后里", L"后里",L"后街", L"后街",L"后羿", L"后羿",L"后稷", L"后稷",L"后座", L"后座",L"后平路", L"后平路",L"后安路", L"后安路",L"后土", L"后土",L"后北街", L"后北街",L"后冠", L"后冠",L"望后石", L"望后石",L"后角", L"后角",L"蚁后", L"蟻后",L"后妃", L"后妃",L"大周后", L"大周后",L"小周后", L"小周后",L"周傑倫", L"周杰倫",L"石梁", L"石樑",L"木梁", L"木樑",L"橫梁", L"橫樑",L"索马里", L"索馬里",L"索馬里", L"索馬里",L"馬格里布", L"馬格里布",L"马格里布", L"馬格里布",L"厄里斯", L"厄里斯",L"公里", L"公里",L"藏历史", L"藏歷史",L"页面", L"頁面",L"頁面", L"頁面",L"方面", L"方面",L"表面", L"表面",L"面条目", L"面條目",L"面條目", L"面條目",L"洋面", L"洋面",L"質朴", L"質樸",L"掣签", L"掣籤",L"酒曲", L"酒麴",L"施舍之道", L"施舍之道",L"升阳", L"昇陽",L"黑松", L"黑松",L"赤松", L"赤松",L"松科", L"松科",L"拜托", L"拜託",L"委托书", L"委託書",L"委托", L"委託",L"信托", L"信託",L"挽詞", L"輓詞",L"挽聯", L"輓聯",L"万象", L"萬象",L"系数", L"係數",L"叶恭弘", L"叶恭弘",L"叶", L"葉",L"置于", L"置於",L"散布于", L"散佈於",L"建国于", L"建國於",L"于美人", L"于美人",L"长于", L"長於",L"短于", L"短於",L"课余", L"課餘",L"节余", L"節餘",L"盈余", L"盈餘",L"残余", L"殘餘",L"其余", L"其餘",L"余款", L"餘款",L"余弦", L"餘弦",L"余地", L"餘地",L"余割", L"餘割",L"余切", L"餘切",L"余下", L"餘下",L"业余", L"業餘",L"余孽", L"餘孽",L"余人", L"餘人",L"编余", L"編余",L"病余", L"病余",L"余力", L"餘力",L"余子", L"餘子",L"余事", L"餘事",L"扶余", L"扶余",L"腐余", L"腐餘",L"富余", L"富餘",L"之余", L"之餘",L"余泽", L"餘澤",L"餘澤", L"餘澤",L"余俗", L"餘俗",L"餘俗", L"餘俗",L"淋余土", L"淋餘土",L"零余", L"零餘",L"余额", L"餘額",L"余款", L"餘款",L"余数", L"餘數",L"余震", L"餘震",L"余三", L"餘三",L"余倍", L"餘倍",L"冗余", L"冗餘",L"林郁方", L"林郁方",L"心愿", L"心願",L"曰云", L"曰云",L"赞颂", L"讚頌",L"赞赏", L"讚賞",L"赞美", L"讚美",L"赞歌", L"讚歌",L"赞扬", L"讚揚",L"赞叹", L"讚歎",L"称赞", L"稱讚",L"大赞辞", L"大讚辭",L"扎实", L"紮實",L"駐扎", L"駐紮",L"新扎", L"新紮",L"紙扎", L"紙紮",L"扎鐵", L"紮鐵",L"扎寨", L"紮寨",L"一扎", L"一紮",L"兩扎", L"兩紮",L"三扎", L"三紮",L"四扎", L"四紮",L"五扎", L"五紮",L"六扎", L"六紮",L"七扎", L"七紮",L"八扎", L"八紮",L"九扎", L"九紮",L"十扎", L"十紮",L"百扎", L"百紮",L"千扎", L"千紮",L"萬扎", L"萬紮",L"占领", L"佔領",L"占超過", L"佔超過",L"占有", L"佔有",L"占据", L"佔據",L"攻占", L"攻佔",L"抢占", L"搶佔",L"侵占", L"侵佔",L"霸占", L"霸佔",L"独占鳌头", L"獨佔鰲頭",L"占城", L"占城",L"折迭", L"摺疊",L"折叠", L"摺疊",L"折損", L"折損",L"折合", L"折合",L"折", L"折",L"象征", L"象徵",L"特征", L"特徵",L"性征", L"性徵",L"征验", L"徵驗",L"征迹", L"徵跡",L"征车", L"徵車",L"征象", L"徵象",L"征調", L"徵調",L"征詢", L"徵詢",L"征聘", L"徵聘",L"征稅", L"徵稅",L"征狀", L"徵狀",L"征求", L"徵求",L"征效", L"徵效",L"征收", L"徵收",L"征怪", L"徵怪",L"征引", L"徵引",L"征圣", L"徵聖",L"征咎", L"徵咎",L"征吏", L"徵吏",L"征召", L"徵召",L"征募", L"徵募",L"征兆", L"徵兆",L"征候", L"徵候",L"征令", L"徵令",L"本征", L"本徵",L"症候群", L"症候群",L"病症", L"病症",L"几只", L"幾隻",L"数只", L"數隻",L"人数只", L"人數只",L"两只", L"兩隻",L"0只", L"0隻",L"1只", L"1隻",L"2只", L"2隻",L"3只", L"3隻",L"4只", L"4隻",L"5只", L"5隻",L"6只", L"6隻",L"7只", L"7隻",L"8只", L"8隻",L"9只", L"9隻",L"零只", L"零隻",L"一只", L"一隻",L"二只", L"二隻",L"三只", L"三隻",L"四只", L"四隻",L"五只", L"五隻",L"六只", L"六隻",L"七只", L"七隻",L"八只", L"八隻",L"九只", L"九隻",L"十只", L"十隻",L"百只", L"百隻",L"千只", L"千隻",L"万只", L"萬隻",L"亿只", L"億隻",L"0多只", L"0多隻",L"十多只", L"十多隻",L"百多只", L"百多隻",L"千多只", L"千多隻",L"万多只", L"萬多隻",L"亿多只", L"億多隻",L"有多只", L"有多隻",L"的多只", L"的多隻",L"和多只", L"和多隻",L"或多只", L"或多隻",L"最多只", L"最多只",L"那只是", L"那只是",L"只说", L"只說",L"只要", L"只要",L"只能", L"只能",L"只有", L"只有",L"只是", L"只是",L"只怕", L"只怕",L"只可", L"只可",L"只会", L"只會",L"别只", L"別只",L"标志", L"標誌",L"网志", L"網誌",L"志异", L"誌異",L"制裁", L"制裁",L"制服", L"制服",L"改制", L"改制",L"控制", L"控制",L"機制", L"機制",L"獨裁制", L"獨裁制",L"限制", L"限制",L"制浆", L"製漿",L"台制", L"台制",L"紧致", L"緊緻",L"李钟郁", L"李鍾郁",L"钱钟书", L"錢鍾書",L"钟欣桐", L"鍾欣桐",L"钟无艳", L"鍾無艷",L"钟情", L"鍾情",L"钟爱", L"鍾愛",L"钟灵", L"鍾靈",L"船钟", L"船鐘",L"注音", L"注音",L"注册", L"註冊",L"关注", L"關注",L"注意", L"注意",L"筑前", L"筑前",L"筑後", L"筑後",L"筑紫", L"筑紫",L"筑波", L"筑波",L"筑州", L"筑州",L"筑肥", L"筑肥",L"筑西", L"筑西",L"筑北", L"筑北",L"肥筑方言", L"肥筑方言",L"筑邦", L"筑邦",L"筑阳", L"筑陽",L"南筑", L"南筑",L"黄钰筑", L"黃鈺筑",L"黃鈺筑", L"黃鈺筑",L"齐庄", L"齊莊",L"鸿案相庄", L"鴻案相莊",L"项庄", L"項莊",L"韦庄", L"韋莊",L"锅庄", L"鍋莊",L"钱庄", L"錢莊",L"郑庄公", L"鄭莊公",L"通庄", L"通莊",L"连庄", L"連莊",L"蒙庄", L"蒙莊",L"茶庄", L"茶莊",L"老庄", L"老莊",L"端庄", L"端莊",L"秋庄稼", L"秋莊稼",L"票庄", L"票莊",L"矜庄", L"矜莊",L"田庄", L"田莊",L"楚庄问鼎", L"楚莊問鼎",L"楚庄绝缨", L"楚莊絕纓",L"楚庄王", L"楚莊王",L"村庄", L"村莊",L"新庄", L"新莊",L"整庄", L"整莊",L"打路庄板", L"打路莊板",L"康庄", L"康莊",L"庄骚", L"莊騷",L"庄院", L"莊院",L"庄重", L"莊重",L"庄语", L"莊語",L"庄舄越吟", L"莊舄越吟",L"庄稼", L"莊稼",L"庄田", L"莊田",L"庄敬", L"莊敬",L"庄房", L"莊房",L"庄户", L"莊戶",L"庄家", L"莊家",L"庄客", L"莊客",L"庄子", L"莊子",L"庄园", L"莊園",L"庄周", L"莊周",L"庄农", L"莊農",L"庄主", L"莊主",L"庄严", L"莊嚴",L"平泉庄", L"平泉莊",L"布庄", L"布莊",L"龜山庄", L"龜山庄",L"香山庄", L"香山庄",L"寶山庄", L"寶山庄",L"冬山庄", L"冬山庄",L"員山庄", L"員山庄",L"松山庄", L"松山庄",L"宝相庄严", L"寶相莊嚴",L"宝庄", L"寶莊",L"官庄", L"官莊",L"坐庄", L"坐莊",L"周庄王", L"周莊王",L"发庄", L"發莊",L"卞庄", L"卞莊",L"包庄", L"包莊",L"剔庄货", L"剔莊貨",L"刘克庄", L"劉克莊",L"冷庄子", L"冷莊子",L"农庄", L"農莊",L"做庄", L"做莊",L"义庄", L"義莊",L"石家庄", L"石家莊",L"获准", L"獲准",L"獲准", L"獲准",L"御准", L"御准",L"准许", L"准許",L"准許", L"准許",L"准將", L"准將",L"准将", L"准將",L"作准", L"作準",L"为准", L"為準",L"不准", L"不准",L"不准確", L"不準確",L"幺", L"么",L"么子", L"么子",L"老么", L"老么",L"么半范畴", L"么半範疇",L"么半範疇", L"么半範疇",L"昆侖", L"崑崙",L"昆仑", L"崑崙",L"岳麓山", L"嶽麓山",L"纳米比亚", L"納米比亞",L"納米比亞", L"納米比亞",L"朝鲜半岛", L"朝鮮半島",L"蒙卡达", L"蒙卡達",L"蒙卡達", L"蒙卡達",L"镰仓", L"鎌倉",L"酒杯", L"酒杯",L"请君入瓮", L"請君入甕",L"瓮安", L"甕安",L"蛋家", L"蜑家",L"痊愈", L"痊癒",L"無厘頭", L"無厘頭",L"水蒸汽", L"水蒸氣",L"水汽", L"水氣",L"杠杆", L"槓桿",L"昵称", L"暱稱",L"无厘头", L"無厘頭",L"带宽", L"頻寬",L"宣泄", L"宣洩",L"图鉴", L"圖鑑",L"噪声", L"噪聲",L"咨询", L"諮詢",L"可乐", L"可樂",L"积极", L"積極",L"参与", L"參與",L"积极参与", L"積極參與",L"积极参加", L"積極參加",L"勋章", L"勳章",L"僵尸", L"殭屍",L"仿佛", L"彷彿",L"么正", L"么正",L"么女", L"么女",L"酰胺", L"醯胺",L"单于", L"單于",L"承担", L"承擔",L"红旗软件", L"紅旗軟件",L"紅旗軟件", L"紅旗軟件",L"金山软件", L"金山軟件",L"金山軟件", L"金山軟件",L"全景软体", L"全景軟體",L"全景軟體", L"全景軟體",L"熊猫软体", L"熊貓軟體",L"熊貓軟體", L"熊貓軟體",L"软体电脑", L"軟體電腦",L"軟體電腦", L"軟體電腦",L"软体动物", L"軟體動物",L"軟體動物", L"軟體動物",L"国家航天局", L"國家航天局",L"航天工业部", L"航天工業部",L"索尼爱力信", L"索尼愛力信",L"索尼中國", L"索尼中國",L"索尼中国", L"索尼中國",L"台灣索尼通訊", L"台灣索尼通訊",L"台湾索尼通讯", L"台灣索尼通訊",L"拿破仑", L"拿破崙",L"拿破侖", L"拿破崙",L"汤加丽", L"湯加麗",L"普里查德", L"普里查德",L"奥尔松", L"奧爾松",L"弈訢", L"奕訢",L"奕訢", L"奕訢",L"奕䜣", L"奕訢",L"弈欣", L"奕訢",L"奕欣", L"奕訢",L"兀术", L"兀朮",L"术赤", L"朮赤",L"有栖川", L"有栖川",L"演算", L"演算",L"透射", L"透射",L"空穴来风", L"空穴來風",L"空穴來風", L"空穴來風",L"氨基酸", L"胺基酸",L"卷舌", L"捲舌",L"阿什克隆", L"阿什克隆",L"老人", L"老人",L"算子", L"算子",L"窗口", L"窗口",L"突尼斯", L"突尼斯",L"程序", L"程序",L"桌球", L"桌球",L"朝鲜", L"朝鮮",L"多明尼加共和國", L"多明尼加共和國",L"梵蒂冈", L"梵蒂岡",L"教廷", L"教廷",L"数码", L"數碼",L"数位", L"數位",L"括号", L"括號",L"循环", L"循環",L"巴士", L"巴士",L"域名", L"域名",L"因子", L"因子",L"喬治亞州", L"喬治亞州",L"南乔治亚", L"南喬治亞",L"共享", L"共享",L"保安", L"保安",L"习用", L"習用",L"乔治亚州", L"喬治亞州",L"乔治亚·", L"喬治亞·",L"互聯網協會", L"互聯網協會",L"互联网协会", L"互聯網協會",L"互聯網工程工作小組", L"互聯網工程工作小組",L"互联网工程工作小组", L"互聯網工程工作小組",L"互聯網結構委員會", L"互聯網結構委員會",L"互联网结构委员会", L"互聯網結構委員會",L"奔驰", L"奔馳",L"齐克隆", L"齊克隆",L"齊克隆", L"齊克隆",L"东加里曼丹", L"東加里曼丹",L"电影集团", L"電影集團",L"電影集團", L"電影集團",L"么九", L"么九",L"水平面", L"水平面",L"水平線", L"水平線",L"短訊", L"短訊",L"电台", L"電台",L"電台", L"電台",L"雪糕", L"雪糕",L"民乐", L"民樂",L"”", L"」",L"“", L"「",L"‘", L"『",L"’", L"』",L"｢", L"「",L"｣", L"」",L"维基共享资源", L"維基共享資源",L"維基共享資源", L"維基共享資源",L"外部链接", L"外部連結",L"外部鏈接", L"外部連結",L"文件存废讨论", L"檔案存廢討論",L"快捷方式重定向", L"捷徑重定向",L"快捷方式列表", L"捷徑列表",L"志愿者回复团队", L"志願者回覆團隊",L"𪚏", L"𪘀",L"𪎍", L"𪋿",L"𪎋", L"䴴",L"𪎊", L"麨",L"𪎉", L"麲",L"𪎈", L"䴬",L"𪉕", L"𪈁",L"𪉔", L"𪄆",L"𪉓", L"𪈼",L"𪉒", L"𪄕",L"𪉐", L"𪃍",L"𪉏", L"𪃏",L"𪉍", L"鵚",L"𪉌", L"𪁖",L"𪉋", L"𪀾",L"𪉊", L"鷨",L"𪉉", L"𪁈",L"𪉆", L"鴲",L"𪉅", L"𪀸",L"𪉄", L"𩿪",L"𪉃", L"鳼",L"𩾎", L"𩽇",L"𩾌", L"鱇",L"𩾊", L"䱬",L"𩾈", L"䱙",L"𩾇", L"鯱",L"𩾆", L"𩸦",L"𩾅", L"𩸃",L"𩾄", L"𩷰",L"𩾃", L"鮸",L"𩾁", L"鯄",L"𩽿", L"𩶰",L"𩽾", L"鮟",L"𩽽", L"𩶱",L"𩽼", L"鯶",L"𩽻", L"𩵹",L"𩽺", L"𩵩",L"𩽹", L"魥",L"𩨐", L"𩧆",L"𩨏", L"䮳",L"𩨍", L"𩥇",L"𩨌", L"𩥑",L"𩨋", L"𩥄",L"𩨊", L"騚",L"𩨉", L"𩤲",L"𩨈", L"騟",L"𩨆", L"𩤙",L"𩨅", L"𩤸",L"𩨄", L"騪",L"𩨂", L"驄",L"𩨁", L"䮞",L"𩨀", L"騔",L"𩧿", L"䮠",L"𩧼", L"𩣺",L"𩧺", L"駶",L"𩧶", L"𩣏",L"𩧵", L"𩢴",L"𩧴", L"駩",L"𩧳", L"𩢸",L"𩧲", L"駧",L"𩧱", L"𩥉",L"𩧰", L"䮝",L"𩧮", L"𩢾",L"𩧭", L"䭿",L"𩧬", L"𩢡",L"𩧫", L"駚",L"𩧪", L"䮾",L"𩧩", L"𩤊",L"𩧨", L"駎",L"𩧦", L"𩡺",L"𩠏", L"𩞦",L"𩠎", L"𩞄",L"𩠌", L"餸",L"𩠋", L"𩝔",L"𩠊", L"𩜵",L"𩠉", L"𩜇",L"𩠈", L"䭃",L"𩠆", L"𩜦",L"𩠃", L"𩛩",L"𩠂", L"𩛆",L"𩠁", L"𩚵",L"𩠀", L"𩚥",L"𩟿", L"𩚛",L"𩙰", L"𩙈",L"𩙯", L"䬝",L"𩙭", L"𩘝",L"𩙬", L"𩘺",L"𩙫", L"颾",L"𩙩", L"𩘀",L"𩙨", L"𩘹",L"𩙧", L"䬞",L"𩙦", L"𩗀",L"𩖗", L"䫴",L"𩖕", L"𩓣",L"𩐀", L"䪗",L"𩏾", L"𩎢",L"𩏽", L"𩏪",L"𩏼", L"䪏",L"𨸎", L"𨷲",L"𨸌", L"𨶮",L"𨸋", L"𨶲",L"𨸊", L"𨶏",L"𨸄", L"䦘",L"𨸂", L"閍",L"𨸁", L"𨳑",L"𨸀", L"𨳕",L"𨱔", L"鐏",L"𨱒", L"鏉",L"𨱑", L"鐄",L"𨱐", L"𨫒",L"𨱏", L"鎝",L"𨱍", L"鎯",L"𨱌", L"鏆",L"𨱋", L"錂",L"𨱊", L"𨧱",L"𨱉", L"鍄",L"𨱇", L"銶",L"𨱅", L"鉁",L"𨱄", L"鈯",L"𨱃", L"鈲",L"𨱂", L"鈋",L"𨱁", L"鈠",L"𨱀", L"𨥛",L"𨰿", L"𨥊",L"𨰾", L"鎷",L"𨐊", L"𨏥",L"𨐉", L"𨎮",L"𨐈", L"輄",L"𨐇", L"𨏠",L"𨐆", L"𨊻",L"𨐅", L"軗",L"𧹖", L"賟",L"𧹕", L"䝻",L"𧹔", L"賬",L"𧹓", L"𧶔",L"𧹒", L"買",L"𦈡", L"繻",L"𦈠", L"䌥",L"𦈟", L"䌝",L"𦈞", L"䌟",L"𦈝", L"繏",L"𦈜", L"䌖",L"𦈛", L"繓",L"𦈚", L"縬",L"𦈙", L"䌰",L"𦈘", L"䌋",L"𦈖", L"䌈",L"𦈕", L"緰",L"𦈔", L"縎",L"𦈓", L"䋿",L"𦈒", L"𦂅",L"𦈑", L"緸",L"𦈏", L"緍",L"𦈎", L"繟",L"𦈌", L"綀",L"𦈋", L"綇",L"𦈉", L"緷",L"𦈈", L"𥿊",L"骔", L"騌",L"馀", L"餘",L"颣", L"纇",L"詟", L"讋",L"襕", L"襴",L"萚", L"蘀",L"苎", L"苧",L"腘", L"膕",L"翚", L"翬",L"翙", L"翽",L"缐", L"線",L"篯", L"籛",L"硵", L"磠",L"硚", L"礄",L"瑸", L"璸",L"琎", L"璡",L"玙", L"璵",L"溇", L"漊",L"涢", L"溳",L"浕", L"濜",L"浉", L"溮",L"沨", L"渢",L"椮", L"槮",L"椫", L"樿",L"椢", L"槶",L"庼", L"廎",L"啰", L"囉",L"啯", L"嘓",L"叇", L"靆",L"叆", L"靉",L"厐", L"龎",L"卧", L"臥",L"净", L"淨",L"伡", L"俥",L"亘", L"亙",L"䶮", L"龑",L"䶊", L"衄",L"䲣", L"䱷",L"䲢", L"鰧",L"䲡", L"鰍",L"䲠", L"鰆",L"䲟", L"鮣",L"䲞", L"𩶘",L"䲝", L"䱽",L"䯅", L"䯀",L"䯄", L"騧",L"䯃", L"𩣑",L"䭪", L"𩞯",L"䩄", L"靦",L"䦷", L"䦟",L"䦶", L"䦛",L"䦆", L"钁",L"䦅", L"鐥",L"䦃", L"鐯",L"䦂", L"䥇",L"䦁", L"𨧜",L"䦀", L"𨦫",L"䥿", L"𨯅",L"䥾", L"䥱",L"䥽", L"鏺",L"䥺", L"釾",L"䢂", L"𨋢",L"䢁", L"𨊸",L"䢀", L"𨊰",L"䞐", L"賰",L"䞎", L"𧶧",L"䞍", L"䝼",L"䞌", L"𧵳",L"䝙", L"貙",L"䜩", L"讌",L"䜧", L"䜀",L"䜥", L"𧩙",L"䙓", L"襬",L"䙌", L"䙡",L"䙊", L"𧜵",L"䘛", L"𧝞",L"䗖", L"螮",L"䓖", L"藭",L"䓕", L"薳",L"䏝", L"膞",L"䎬", L"䎱",L"䍀", L"襤",L"䌿", L"䋹",L"䌾", L"䋻",L"䌽", L"綵",L"䌼", L"綐",L"䌻", L"䋚",L"䌺", L"䋙",L"䅟", L"穇",L"䅉", L"稏",L"㻘", L"𤪺",L"㻏", L"𤫩",L"㺍", L"獱",L"㶽", L"煱",L"㶶", L"燶",L"㶉", L"鸂",L"㳽", L"瀰",L"㳠", L"澾",L"㲿", L"瀇",L"㱩", L"殰",L"㭎", L"棡",L"㨫", L"㩜",L"㧐", L"㩳",L"㧏", L"掆",L"㤘", L"㥮",L"㟆", L"㠏",L"㛿", L"𡠹",L"㛠", L"𡢃",L"㛟", L"𡞵",L"㘎", L"㘚",L"㖊", L"噚",L"㔉", L"劚",L"㓥", L"劏",L"㑩", L"儸",L"㑇", L"㑳",L"㐹", L"㑶",L"㐷", L"傌",L"䯄", L"騧",L"钅卢", L"鑪",L"钅达", L"鐽",L"钅仑", L"錀",L"齄", L"齇",L"麽", L"麼",L"鞲", L"韝",L"闲", L"閒",L"镎", L"錼",L"镋", L"钂",L"镅", L"鋂",L"锿", L"鑀",L"锝", L"鎝",L"锎", L"鉲",L"酰", L"醯",L"谥", L"諡",L"裥", L"襉",L"蝎", L"蠍",L"莼", L"蓴",L"药", L"藥",L"绷", L"繃",L"绱", L"鞝",L"碱", L"鹼",L"硷", L"鹼",L"彞", L"彝",L"彝", L"彝",L"幞", L"襆",L"啮", L"齧",L"卺", L"巹",L"么", L"麼",L"水里鄉", L"水里鄉",L"水里乡", L"水里鄉",L"水里溪", L"水里溪",L"水里濁水溪", L"水里濁水溪",L"水里浊水溪", L"水里濁水溪"};
static WCHAR* big2gb2[] = { L"㑳", L"㑇", L"㞞", L"𪨊", L"㠏", L"㟆", L"㩜", L"㨫", L"䉬", L"𫂈", L"䊷", L"䌶", L"䋙", L"䌺", L"䋻", L"䌾", L"䌈", L"𦈖", L"䝼", L"䞍", L"䪏", L"𩏼", L"䪗", L"𩐀", L"䪘", L"𩏿", L"䫴", L"𩖗", L"䬘", L"𩙮", L"䬝", L"𩙯", L"䭀", L"𩠇", L"䭃", L"𩠈", L"䭿", L"𩧭", L"䮝", L"𩧰", L"䮞", L"𩨁", L"䮠", L"𩧿", L"䮳", L"𩨏", L"䮾", L"𩧪", L"䯀", L"䯅", L"䰾", L"鲃", L"䱙", L"𩾈", L"䱬", L"𩾊", L"䱰", L"𩾋", L"䱷", L"䲣", L"䱽", L"䲝", L"䲁", L"鳚", L"䲰", L"𪉂", L"䴬", L"𪎈", L"䴴", L"𪎋", L"丟", L"丢", L"並", L"并", L"乾", L"干", L"亂", L"乱", L"亙", L"亘", L"亞", L"亚", L"佇", L"伫", L"佈", L"布", L"佔", L"占", L"併", L"并", L"來", L"来", L"侖", L"仑", L"侶", L"侣", L"俁", L"俣", L"係", L"系", L"俔", L"伣", L"俠", L"侠", L"倀", L"伥", L"倆", L"俩", L"倈", L"俫", L"倉", L"仓", L"個", L"个", L"們", L"们", L"倖", L"幸", L"倫", L"伦", L"偉", L"伟", L"側", L"侧", L"偵", L"侦", L"偽", L"伪", L"傑", L"杰", L"傖", L"伧", L"傘", L"伞", L"備", L"备", L"傢", L"家", L"傭", L"佣", L"傯", L"偬", L"傳", L"传", L"傴", L"伛", L"債", L"债", L"傷", L"伤", L"傾", L"倾", L"僂", L"偻", L"僅", L"仅", L"僉", L"佥", L"僑", L"侨", L"僕", L"仆", L"僞", L"伪", L"僥", L"侥", L"僨", L"偾", L"僱", L"雇", L"價", L"价", L"儀", L"仪", L"儂", L"侬", L"億", L"亿", L"儈", L"侩", L"儉", L"俭", L"儐", L"傧", L"儔", L"俦", L"儕", L"侪", L"儘", L"尽", L"償", L"偿", L"優", L"优", L"儲", L"储", L"儷", L"俪", L"儸", L"㑩", L"儺", L"傩", L"儻", L"傥", L"儼", L"俨", L"兇", L"凶", L"兌", L"兑", L"兒", L"儿", L"兗", L"兖", L"內", L"内", L"兩", L"两", L"冊", L"册", L"冪", L"幂", L"凈", L"净", L"凍", L"冻", L"凙", L"𪞝", L"凜", L"凛", L"凱", L"凯", L"別", L"别", L"刪", L"删", L"剄", L"刭", L"則", L"则", L"剋", L"克", L"剎", L"刹", L"剗", L"刬", L"剛", L"刚", L"剝", L"剥", L"剮", L"剐", L"剴", L"剀", L"創", L"创", L"剷", L"铲", L"劃", L"划", L"劇", L"剧", L"劉", L"刘", L"劊", L"刽", L"劌", L"刿", L"劍", L"剑", L"劏", L"㓥", L"劑", L"剂", L"劚", L"㔉", L"勁", L"劲", L"動", L"动", L"務", L"务", L"勛", L"勋", L"勝", L"胜", L"勞", L"劳", L"勢", L"势", L"勩", L"勚", L"勱", L"劢", L"勳", L"勋", L"勵", L"励", L"勸", L"劝", L"勻", L"匀", L"匭", L"匦", L"匯", L"汇", L"匱", L"匮", L"區", L"区", L"協", L"协", L"卻", L"却", L"卽", L"即", L"厙", L"厍", L"厠", L"厕", L"厤", L"历", L"厭", L"厌", L"厲", L"厉", L"厴", L"厣", L"參", L"参", L"叄", L"叁", L"叢", L"丛", L"吒", L"咤", L"吳", L"吴", L"吶", L"呐", L"呂", L"吕", L"咼", L"呙", L"員", L"员", L"唄", L"呗", L"唚", L"吣", L"問", L"问", L"啓", L"启", L"啞", L"哑", L"啟", L"启", L"啢", L"唡", L"喎", L"㖞", L"喚", L"唤", L"喪", L"丧", L"喫", L"吃", L"喬", L"乔", L"單", L"单", L"喲", L"哟", L"嗆", L"呛", L"嗇", L"啬", L"嗊", L"唝", L"嗎", L"吗", L"嗚", L"呜", L"嗩", L"唢", L"嗰", L"𠮶", L"嗶", L"哔", L"嗹", L"𪡏", L"嘆", L"叹", L"嘍", L"喽", L"嘔", L"呕", L"嘖", L"啧", L"嘗", L"尝", L"嘜", L"唛", L"嘩", L"哗", L"嘮", L"唠", L"嘯", L"啸", L"嘰", L"叽", L"嘵", L"哓", L"嘸", L"呒", L"嘽", L"啴", L"噁", L"恶", L"噓", L"嘘", L"噚", L"㖊", L"噝", L"咝", L"噠", L"哒", L"噥", L"哝", L"噦", L"哕", L"噯", L"嗳", L"噲", L"哙", L"噴", L"喷", L"噸", L"吨", L"噹", L"当", L"嚀", L"咛", L"嚇", L"吓", L"嚌", L"哜", L"嚐", L"尝", L"嚕", L"噜", L"嚙", L"啮", L"嚥", L"咽", L"嚦", L"呖", L"嚨", L"咙", L"嚮", L"向", L"嚲", L"亸", L"嚳", L"喾", L"嚴", L"严", L"嚶", L"嘤", L"囀", L"啭", L"囁", L"嗫", L"囂", L"嚣", L"囅", L"冁", L"囈", L"呓", L"囌", L"苏", L"囑", L"嘱", L"囪", L"囱", L"圇", L"囵", L"國", L"国", L"圍", L"围", L"園", L"园", L"圓", L"圆", L"圖", L"图", L"團", L"团", L"圞", L"𪢮", L"垵", L"埯", L"埡", L"垭", L"埰", L"采", L"執", L"执", L"堅", L"坚", L"堊", L"垩", L"堖", L"垴", L"堝", L"埚", L"堯", L"尧", L"報", L"报", L"場", L"场", L"塊", L"块", L"塋", L"茔", L"塏", L"垲", L"塒", L"埘", L"塗", L"涂", L"塚", L"冢", L"塢", L"坞", L"塤", L"埙", L"塵", L"尘", L"塹", L"堑", L"墊", L"垫", L"墜", L"坠", L"墮", L"堕", L"墰", L"坛", L"墳", L"坟", L"墻", L"墙", L"墾", L"垦", L"壇", L"坛", L"壈", L"𡒄", L"壋", L"垱", L"壓", L"压", L"壘", L"垒", L"壙", L"圹", L"壚", L"垆", L"壜", L"坛", L"壞", L"坏", L"壟", L"垄", L"壠", L"垅", L"壢", L"坜", L"壩", L"坝", L"壯", L"壮", L"壺", L"壶", L"壼", L"壸", L"壽", L"寿", L"夠", L"够", L"夢", L"梦", L"夥", L"伙", L"夾", L"夹", L"奐", L"奂", L"奧", L"奥", L"奩", L"奁", L"奪", L"夺", L"奬", L"奖", L"奮", L"奋", L"奼", L"姹", L"妝", L"妆", L"姍", L"姗", L"姦", L"奸", L"娛", L"娱", L"婁", L"娄", L"婦", L"妇", L"婭", L"娅", L"媧", L"娲", L"媯", L"妫", L"媼", L"媪", L"媽", L"妈", L"嫗", L"妪", L"嫵", L"妩", L"嫻", L"娴", L"嫿", L"婳", L"嬀", L"妫", L"嬈", L"娆", L"嬋", L"婵", L"嬌", L"娇", L"嬙", L"嫱", L"嬡", L"嫒", L"嬤", L"嬷", L"嬪", L"嫔", L"嬰", L"婴", L"嬸", L"婶", L"孌", L"娈", L"孫", L"孙", L"學", L"学", L"孿", L"孪", L"宮", L"宫", L"寀", L"采", L"寢", L"寝", L"實", L"实", L"寧", L"宁", L"審", L"审", L"寫", L"写", L"寬", L"宽", L"寵", L"宠", L"寶", L"宝", L"將", L"将", L"專", L"专", L"尋", L"寻", L"對", L"对", L"導", L"导", L"尷", L"尴", L"屆", L"届", L"屍", L"尸", L"屓", L"屃", L"屜", L"屉", L"屢", L"屡", L"層", L"层", L"屨", L"屦", L"屩", L"𪨗", L"屬", L"属", L"岡", L"冈", L"峴", L"岘", L"島", L"岛", L"峽", L"峡", L"崍", L"崃", L"崑", L"昆", L"崗", L"岗", L"崙", L"仑", L"崢", L"峥", L"崬", L"岽", L"嵐", L"岚", L"嵗", L"岁", L"嶁", L"嵝", L"嶄", L"崭", L"嶇", L"岖", L"嶔", L"嵚", L"嶗", L"崂", L"嶠", L"峤", L"嶢", L"峣", L"嶧", L"峄", L"嶮", L"崄", L"嶴", L"岙", L"嶸", L"嵘", L"嶺", L"岭", L"嶼", L"屿", L"嶽", L"岳", L"巋", L"岿", L"巒", L"峦", L"巔", L"巅", L"巖", L"岩", L"巰", L"巯", L"巹", L"卺", L"帥", L"帅", L"師", L"师", L"帳", L"帐", L"帶", L"带", L"幀", L"帧", L"幃", L"帏", L"幗", L"帼", L"幘", L"帻", L"幟", L"帜", L"幣", L"币", L"幫", L"帮", L"幬", L"帱", L"幹", L"干", L"幺", L"么", L"幾", L"几", L"庫", L"库", L"廁", L"厕", L"廂", L"厢", L"廄", L"厩", L"廈", L"厦", L"廚", L"厨", L"廝", L"厮", L"廟", L"庙", L"廠", L"厂", L"廡", L"庑", L"廢", L"废", L"廣", L"广", L"廩", L"廪", L"廬", L"庐", L"廳", L"厅", L"弒", L"弑", L"弔", L"吊", L"弳", L"弪", L"張", L"张", L"強", L"强", L"彆", L"别", L"彈", L"弹", L"彌", L"弥", L"彎", L"弯", L"彙", L"汇", L"彞", L"彝", L"彥", L"彦", L"後", L"后", L"徑", L"径", L"從", L"从", L"徠", L"徕", L"復", L"复", L"徵", L"征", L"徹", L"彻", L"恆", L"恒", L"恥", L"耻", L"悅", L"悦", L"悞", L"悮", L"悵", L"怅", L"悶", L"闷", L"惡", L"恶", L"惱", L"恼", L"惲", L"恽", L"惻", L"恻", L"愛", L"爱", L"愜", L"惬", L"愨", L"悫", L"愴", L"怆", L"愷", L"恺", L"愾", L"忾", L"慄", L"栗", L"態", L"态", L"慍", L"愠", L"慘", L"惨", L"慚", L"惭", L"慟", L"恸", L"慣", L"惯", L"慤", L"悫", L"慪", L"怄", L"慫", L"怂", L"慮", L"虑", L"慳", L"悭", L"慶", L"庆", L"慼", L"戚", L"慾", L"欲", L"憂", L"忧", L"憊", L"惫", L"憐", L"怜", L"憑", L"凭", L"憒", L"愦", L"憚", L"惮", L"憤", L"愤", L"憫", L"悯", L"憮", L"怃", L"憲", L"宪", L"憶", L"忆", L"懇", L"恳", L"應", L"应", L"懌", L"怿", L"懍", L"懔", L"懞", L"蒙", L"懟", L"怼", L"懣", L"懑", L"懨", L"恹", L"懲", L"惩", L"懶", L"懒", L"懷", L"怀", L"懸", L"悬", L"懺", L"忏", L"懼", L"惧", L"懾", L"慑", L"戀", L"恋", L"戇", L"戆", L"戔", L"戋", L"戧", L"戗", L"戩", L"戬", L"戰", L"战", L"戱", L"戯", L"戲", L"戏", L"戶", L"户", L"拋", L"抛", L"拚", L"拼", L"挩", L"捝", L"挱", L"挲", L"挾", L"挟", L"捨", L"舍", L"捫", L"扪", L"捱", L"挨", L"捲", L"卷", L"掃", L"扫", L"掄", L"抡", L"掗", L"挜", L"掙", L"挣", L"掛", L"挂", L"採", L"采", L"揀", L"拣", L"揚", L"扬", L"換", L"换", L"揮", L"挥", L"損", L"损", L"搖", L"摇", L"搗", L"捣", L"搵", L"揾", L"搶", L"抢", L"摑", L"掴", L"摜", L"掼", L"摟", L"搂", L"摯", L"挚", L"摳", L"抠", L"摶", L"抟", L"摺", L"折", L"摻", L"掺", L"撈", L"捞", L"撏", L"挦", L"撐", L"撑", L"撓", L"挠", L"撝", L"㧑", L"撟", L"挢", L"撣", L"掸", L"撥", L"拨", L"撫", L"抚", L"撲", L"扑", L"撳", L"揿", L"撻", L"挞", L"撾", L"挝", L"撿", L"捡", L"擁", L"拥", L"擄", L"掳", L"擇", L"择", L"擊", L"击", L"擋", L"挡", L"擓", L"㧟", L"擔", L"担", L"據", L"据", L"擠", L"挤", L"擬", L"拟", L"擯", L"摈", L"擰", L"拧", L"擱", L"搁", L"擲", L"掷", L"擴", L"扩", L"擷", L"撷", L"擺", L"摆", L"擻", L"擞", L"擼", L"撸", L"擾", L"扰", L"攄", L"摅", L"攆", L"撵", L"攏", L"拢", L"攔", L"拦", L"攖", L"撄", L"攙", L"搀", L"攛", L"撺", L"攜", L"携", L"攝", L"摄", L"攢", L"攒", L"攣", L"挛", L"攤", L"摊", L"攪", L"搅", L"攬", L"揽", L"敗", L"败", L"敘", L"叙", L"敵", L"敌", L"數", L"数", L"斂", L"敛", L"斃", L"毙", L"斕", L"斓", L"斬", L"斩", L"斷", L"断", L"於", L"于", L"旂", L"旗", L"旣", L"既", L"昇", L"升", L"時", L"时", L"晉", L"晋", L"晝", L"昼", L"暈", L"晕", L"暉", L"晖", L"暘", L"旸", L"暢", L"畅", L"暫", L"暂", L"曄", L"晔", L"曆", L"历", L"曇", L"昙", L"曉", L"晓", L"曏", L"向", L"曖", L"暧", L"曠", L"旷", L"曨", L"昽", L"曬", L"晒", L"書", L"书", L"會", L"会", L"朧", L"胧", L"朮", L"术", L"東", L"东", L"杴", L"锨", L"柵", L"栅", L"桿", L"杆", L"梔", L"栀", L"梘", L"枧", L"條", L"条", L"梟", L"枭", L"梲", L"棁", L"棄", L"弃", L"棊", L"棋", L"棖", L"枨", L"棗", L"枣", L"棟", L"栋", L"棡", L"", L"棧", L"栈", L"棲", L"栖", L"棶", L"梾", L"椏", L"桠", L"楊", L"杨", L"楓", L"枫", L"楨", L"桢", L"業", L"业", L"極", L"极", L"榦", L"干", L"榪", L"杩", L"榮", L"荣", L"榲", L"榅", L"榿", L"桤", L"構", L"构", L"槍", L"枪", L"槓", L"杠", L"槤", L"梿", L"槧", L"椠", L"槨", L"椁", L"槳", L"桨", L"樁", L"桩", L"樂", L"乐", L"樅", L"枞", L"樑", L"梁", L"樓", L"楼", L"標", L"标", L"樞", L"枢", L"樣", L"样", L"樸", L"朴", L"樹", L"树", L"樺", L"桦", L"橈", L"桡", L"橋", L"桥", L"機", L"机", L"橢", L"椭", L"橫", L"横", L"檁", L"檩", L"檉", L"柽", L"檔", L"档", L"檜", L"桧", L"檟", L"槚", L"檢", L"检", L"檣", L"樯", L"檮", L"梼", L"檯", L"台", L"檳", L"槟", L"檸", L"柠", L"檻", L"槛", L"櫃", L"柜", L"櫓", L"橹", L"櫚", L"榈", L"櫛", L"栉", L"櫝", L"椟", L"櫞", L"橼", L"櫟", L"栎", L"櫥", L"橱", L"櫧", L"槠", L"櫨", L"栌", L"櫪", L"枥", L"櫫", L"橥", L"櫬", L"榇", L"櫱", L"蘖", L"櫳", L"栊", L"櫸", L"榉", L"櫻", L"樱", L"欄", L"栏", L"欅", L"榉", L"權", L"权", L"欏", L"椤", L"欒", L"栾", L"欖", L"榄", L"欞", L"棂", L"欽", L"钦", L"歎", L"叹", L"歐", L"欧", L"歟", L"欤", L"歡", L"欢", L"歲", L"岁", L"歷", L"历", L"歸", L"归", L"歿", L"殁", L"殘", L"残", L"殞", L"殒", L"殤", L"殇", L"殨", L"㱮", L"殫", L"殚", L"殭", L"僵", L"殮", L"殓", L"殯", L"殡", L"殰", L"㱩", L"殲", L"歼", L"殺", L"杀", L"殻", L"壳", L"殼", L"壳", L"毀", L"毁", L"毆", L"殴", L"毿", L"毵", L"氂", L"牦", L"氈", L"毡", L"氌", L"氇", L"氣", L"气", L"氫", L"氢", L"氬", L"氩", L"氳", L"氲", L"汙", L"污", L"決", L"决", L"沒", L"没", L"沖", L"冲", L"況", L"况", L"泝", L"溯", L"洩", L"泄", L"洶", L"汹", L"浹", L"浃", L"涇", L"泾", L"涼", L"凉", L"淒", L"凄", L"淚", L"泪", L"淥", L"渌", L"淨", L"净", L"淩", L"凌", L"淪", L"沦", L"淵", L"渊", L"淶", L"涞", L"淺", L"浅", L"渙", L"涣", L"減", L"减", L"渦", L"涡", L"測", L"测", L"渾", L"浑", L"湊", L"凑", L"湞", L"浈", L"湧", L"涌", L"湯", L"汤", L"溈", L"沩", L"準", L"准", L"溝", L"沟", L"溫", L"温", L"滄", L"沧", L"滅", L"灭", L"滌", L"涤", L"滎", L"荥", L"滙", L"汇", L"滬", L"沪", L"滯", L"滞", L"滲", L"渗", L"滷", L"卤", L"滸", L"浒", L"滻", L"浐", L"滾", L"滚", L"滿", L"满", L"漁", L"渔", L"漚", L"沤", L"漢", L"汉", L"漣", L"涟", L"漬", L"渍", L"漲", L"涨", L"漵", L"溆", L"漸", L"渐", L"漿", L"浆", L"潁", L"颍", L"潑", L"泼", L"潔", L"洁", L"潙", L"沩", L"潛", L"潜", L"潤", L"润", L"潯", L"浔", L"潰", L"溃", L"潷", L"滗", L"潿", L"涠", L"澀", L"涩", L"澆", L"浇", L"澇", L"涝", L"澐", L"沄", L"澗", L"涧", L"澠", L"渑", L"澤", L"泽", L"澦", L"滪", L"澩", L"泶", L"澮", L"浍", L"澱", L"淀", L"澾", L"㳠", L"濁", L"浊", L"濃", L"浓", L"濕", L"湿", L"濘", L"泞", L"濟", L"济", L"濤", L"涛", L"濫", L"滥", L"濰", L"潍", L"濱", L"滨", L"濺", L"溅", L"濼", L"泺", L"濾", L"滤", L"瀅", L"滢", L"瀆", L"渎", L"瀇", L"㲿", L"瀉", L"泻", L"瀋", L"沈", L"瀏", L"浏", L"瀕", L"濒", L"瀘", L"泸", L"瀝", L"沥", L"瀟", L"潇", L"瀠", L"潆", L"瀦", L"潴", L"瀧", L"泷", L"瀨", L"濑", L"瀰", L"弥", L"瀲", L"潋", L"瀾", L"澜", L"灃", L"沣", L"灄", L"滠", L"灑", L"洒", L"灕", L"漓", L"灘", L"滩", L"灝", L"灏", L"灠", L"漤", L"灣", L"湾", L"灤", L"滦", L"灧", L"滟", L"災", L"灾", L"為", L"为", L"烏", L"乌", L"烴", L"烃", L"無", L"无", L"煉", L"炼", L"煒", L"炜", L"煙", L"烟", L"煢", L"茕", L"煥", L"焕", L"煩", L"烦", L"煬", L"炀", L"煱", L"㶽", L"熅", L"煴", L"熒", L"荧", L"熗", L"炝", L"熱", L"热", L"熲", L"颎", L"熾", L"炽", L"燁", L"烨", L"燈", L"灯", L"燉", L"炖", L"燒", L"烧", L"燙", L"烫", L"燜", L"焖", L"營", L"营", L"燦", L"灿", L"燬", L"毁", L"燭", L"烛", L"燴", L"烩", L"燶", L"㶶", L"燼", L"烬", L"燾", L"焘", L"爍", L"烁", L"爐", L"炉", L"爛", L"烂", L"爭", L"争", L"爲", L"为", L"爺", L"爷", L"爾", L"尔", L"牆", L"墙", L"牘", L"牍", L"牽", L"牵", L"犖", L"荦", L"犢", L"犊", L"犧", L"牺", L"狀", L"状", L"狹", L"狭", L"狽", L"狈", L"猙", L"狰", L"猶", L"犹", L"猻", L"狲", L"獁", L"犸", L"獃", L"呆", L"獄", L"狱", L"獅", L"狮", L"獎", L"奖", L"獨", L"独", L"獪", L"狯", L"獫", L"猃", L"獮", L"狝", L"獰", L"狞", L"獱", L"㺍", L"獲", L"获", L"獵", L"猎", L"獷", L"犷", L"獸", L"兽", L"獺", L"獭", L"獻", L"献", L"獼", L"猕", L"玀", L"猡", L"現", L"现", L"琺", L"珐", L"琿", L"珲", L"瑋", L"玮", L"瑒", L"玚", L"瑣", L"琐", L"瑤", L"瑶", L"瑩", L"莹", L"瑪", L"玛", L"瑲", L"玱", L"瑽", L"𪻐", L"璉", L"琏", L"璣", L"玑", L"璦", L"瑷", L"璫", L"珰", L"環", L"环", L"璽", L"玺", L"瓊", L"琼", L"瓏", L"珑", L"瓔", L"璎", L"瓚", L"瓒", L"甌", L"瓯", L"甕", L"瓮", L"產", L"产", L"産", L"产", L"甦", L"苏", L"畝", L"亩", L"畢", L"毕", L"畫", L"画", L"異", L"异", L"畵", L"画", L"當", L"当", L"疇", L"畴", L"疊", L"叠", L"痙", L"痉", L"痠", L"酸", L"痾", L"疴", L"瘂", L"痖", L"瘋", L"疯", L"瘍", L"疡", L"瘓", L"痪", L"瘞", L"瘗", L"瘡", L"疮", L"瘧", L"疟", L"瘮", L"瘆", L"瘲", L"疭", L"瘺", L"瘘", L"瘻", L"瘘", L"療", L"疗", L"癆", L"痨", L"癇", L"痫", L"癉", L"瘅", L"癒", L"愈", L"癘", L"疠", L"癟", L"瘪", L"癡", L"痴", L"癢", L"痒", L"癤", L"疖", L"癥", L"症", L"癧", L"疬", L"癩", L"癞", L"癬", L"癣", L"癭", L"瘿", L"癮", L"瘾", L"癰", L"痈", L"癱", L"瘫", L"癲", L"癫", L"發", L"发", L"皚", L"皑", L"皰", L"疱", L"皸", L"皲", L"皺", L"皱", L"盃", L"杯", L"盜", L"盗", L"盞", L"盏", L"盡", L"尽", L"監", L"监", L"盤", L"盘", L"盧", L"卢", L"盪", L"荡", L"眥", L"眦", L"眾", L"众", L"睍", L"𪾢", L"睏", L"困", L"睜", L"睁", L"睞", L"睐", L"瞘", L"眍", L"瞜", L"䁖", L"瞞", L"瞒", L"瞭", L"了", L"瞶", L"瞆", L"瞼", L"睑", L"矇", L"蒙", L"矓", L"眬", L"矚", L"瞩", L"矯", L"矫", L"硃", L"朱", L"硜", L"硁", L"硤", L"硖", L"硨", L"砗", L"硯", L"砚", L"碕", L"埼", L"碩", L"硕", L"碭", L"砀", L"碸", L"砜", L"確", L"确", L"碼", L"码", L"磑", L"硙", L"磚", L"砖", L"磣", L"碜", L"磧", L"碛", L"磯", L"矶", L"磽", L"硗", L"礆", L"硷", L"礎", L"础", L"礙", L"碍", L"礦", L"矿", L"礪", L"砺", L"礫", L"砾", L"礬", L"矾", L"礱", L"砻", L"祘", L"算", L"祿", L"禄", L"禍", L"祸", L"禎", L"祯", L"禕", L"祎", L"禡", L"祃", L"禦", L"御", L"禪", L"禅", L"禮", L"礼", L"禰", L"祢", L"禱", L"祷", L"禿", L"秃", L"秈", L"籼", L"稅", L"税", L"稈", L"秆", L"稏", L"䅉", L"稜", L"棱", L"稟", L"禀", L"種", L"种", L"稱", L"称", L"穀", L"谷", L"穌", L"稣", L"積", L"积", L"穎", L"颖", L"穠", L"秾", L"穡", L"穑", L"穢", L"秽", L"穩", L"稳", L"穫", L"获", L"穭", L"稆", L"窩", L"窝", L"窪", L"洼", L"窮", L"穷", L"窯", L"窑", L"窵", L"窎", L"窶", L"窭", L"窺", L"窥", L"竄", L"窜", L"竅", L"窍", L"竇", L"窦", L"竈", L"灶", L"竊", L"窃", L"竪", L"竖", L"競", L"竞", L"筆", L"笔", L"筍", L"笋", L"筧", L"笕", L"筴", L"䇲", L"箇", L"个", L"箋", L"笺", L"箏", L"筝", L"節", L"节", L"範", L"范", L"築", L"筑", L"篋", L"箧", L"篔", L"筼", L"篤", L"笃", L"篩", L"筛", L"篳", L"筚", L"簀", L"箦", L"簍", L"篓", L"簑", L"蓑", L"簞", L"箪", L"簡", L"简", L"簣", L"篑", L"簫", L"箫", L"簹", L"筜", L"簽", L"签", L"簾", L"帘", L"籃", L"篮", L"籌", L"筹", L"籙", L"箓", L"籜", L"箨", L"籟", L"籁", L"籠", L"笼", L"籤", L"签", L"籩", L"笾", L"籪", L"簖", L"籬", L"篱", L"籮", L"箩", L"籲", L"吁", L"粵", L"粤", L"糝", L"糁", L"糞", L"粪", L"糧", L"粮", L"糰", L"团", L"糲", L"粝", L"糴", L"籴", L"糶", L"粜", L"糹", L"纟", L"糾", L"纠", L"紀", L"纪", L"紂", L"纣", L"約", L"约", L"紅", L"红", L"紆", L"纡", L"紇", L"纥", L"紈", L"纨", L"紉", L"纫", L"紋", L"纹", L"納", L"纳", L"紐", L"纽", L"紓", L"纾", L"純", L"纯", L"紕", L"纰", L"紖", L"纼", L"紗", L"纱", L"紘", L"纮", L"紙", L"纸", L"級", L"级", L"紛", L"纷", L"紜", L"纭", L"紝", L"纴", L"紡", L"纺", L"紬", L"䌷", L"紮", L"扎", L"細", L"细", L"紱", L"绂", L"紲", L"绁", L"紳", L"绅", L"紵", L"纻", L"紹", L"绍", L"紺", L"绀", L"紼", L"绋", L"紿", L"绐", L"絀", L"绌", L"終", L"终", L"組", L"组", L"絅", L"䌹", L"絆", L"绊", L"絎", L"绗", L"結", L"结", L"絕", L"绝", L"絛", L"绦", L"絝", L"绔", L"絞", L"绞", L"絡", L"络", L"絢", L"绚", L"給", L"给", L"絨", L"绒", L"絰", L"绖", L"統", L"统", L"絲", L"丝", L"絳", L"绛", L"絶", L"绝", L"絹", L"绢", L"絺", L"𫄨", L"綁", L"绑", L"綃", L"绡", L"綆", L"绠", L"綈", L"绨", L"綉", L"绣", L"綌", L"绤", L"綏", L"绥", L"綐", L"䌼", L"經", L"经", L"綜", L"综", L"綞", L"缍", L"綠", L"绿", L"綢", L"绸", L"綣", L"绻", L"綫", L"线", L"綬", L"绶", L"維", L"维", L"綯", L"绹", L"綰", L"绾", L"綱", L"纲", L"網", L"网", L"綳", L"绷", L"綴", L"缀", L"綵", L"彩", L"綸", L"纶", L"綹", L"绺", L"綺", L"绮", L"綻", L"绽", L"綽", L"绰", L"綾", L"绫", L"綿", L"绵", L"緄", L"绲", L"緇", L"缁", L"緊", L"紧", L"緋", L"绯", L"緑", L"绿", L"緒", L"绪", L"緓", L"绬", L"緔", L"绱", L"緗", L"缃", L"緘", L"缄", L"緙", L"缂", L"線", L"线", L"緝", L"缉", L"緞", L"缎", L"締", L"缔", L"緡", L"缗", L"緣", L"缘", L"緦", L"缌", L"編", L"编", L"緩", L"缓", L"緬", L"缅", L"緯", L"纬", L"緱", L"缑", L"緲", L"缈", L"練", L"练", L"緶", L"缏", L"緹", L"缇", L"緻", L"致", L"縈", L"萦", L"縉", L"缙", L"縊", L"缢", L"縋", L"缒", L"縐", L"绉", L"縑", L"缣", L"縕", L"缊", L"縗", L"缞", L"縛", L"缚", L"縝", L"缜", L"縞", L"缟", L"縟", L"缛", L"縣", L"县", L"縧", L"绦", L"縫", L"缝", L"縭", L"缡", L"縮", L"缩", L"縱", L"纵", L"縲", L"缧", L"縳", L"䌸", L"縴", L"纤", L"縵", L"缦", L"縶", L"絷", L"縷", L"缕", L"縹", L"缥", L"總", L"总", L"績", L"绩", L"繃", L"绷", L"繅", L"缫", L"繆", L"缪", L"繐", L"穗", L"繒", L"缯", L"織", L"织", L"繕", L"缮", L"繚", L"缭", L"繞", L"绕", L"繡", L"绣", L"繢", L"缋", L"繩", L"绳", L"繪", L"绘", L"繫", L"系", L"繭", L"茧", L"繮", L"缰", L"繯", L"缳", L"繰", L"缲", L"繳", L"缴", L"繸", L"䍁", L"繹", L"绎", L"繼", L"继", L"繽", L"缤", L"繾", L"缱", L"繿", L"䍀", L"纁", L"𫄸", L"纈", L"缬", L"纊", L"纩", L"續", L"续", L"纍", L"累", L"纏", L"缠", L"纓", L"缨", L"纔", L"才", L"纖", L"纤", L"纘", L"缵", L"纜", L"缆", L"缽", L"钵", L"罈", L"坛", L"罌", L"罂", L"罎", L"坛", L"罰", L"罚", L"罵", L"骂", L"罷", L"罢", L"羅", L"罗", L"羆", L"罴", L"羈", L"羁", L"羋", L"芈", L"羥", L"羟", L"羨", L"羡", L"義", L"义", L"習", L"习", L"翹", L"翘", L"耬", L"耧", L"耮", L"耢", L"聖", L"圣", L"聞", L"闻", L"聯", L"联", L"聰", L"聪", L"聲", L"声", L"聳", L"耸", L"聵", L"聩", L"聶", L"聂", L"職", L"职", L"聹", L"聍", L"聽", L"听", L"聾", L"聋", L"肅", L"肃", L"脅", L"胁", L"脈", L"脉", L"脛", L"胫", L"脣", L"唇", L"脫", L"脱", L"脹", L"胀", L"腎", L"肾", L"腖", L"胨", L"腡", L"脶", L"腦", L"脑", L"腫", L"肿", L"腳", L"脚", L"腸", L"肠", L"膃", L"腽", L"膚", L"肤", L"膠", L"胶", L"膩", L"腻", L"膽", L"胆", L"膾", L"脍", L"膿", L"脓", L"臉", L"脸", L"臍", L"脐", L"臏", L"膑", L"臘", L"腊", L"臚", L"胪", L"臟", L"脏", L"臠", L"脔", L"臢", L"臜", L"臥", L"卧", L"臨", L"临", L"臺", L"台", L"與", L"与", L"興", L"兴", L"舉", L"举", L"舊", L"旧", L"舘", L"馆", L"艙", L"舱", L"艤", L"舣", L"艦", L"舰", L"艫", L"舻", L"艱", L"艰", L"艷", L"艳", L"芻", L"刍", L"苧", L"苎", L"茲", L"兹", L"荊", L"荆", L"莊", L"庄", L"莖", L"茎", L"莢", L"荚", L"莧", L"苋", L"華", L"华", L"菴", L"庵", L"萇", L"苌", L"萊", L"莱", L"萬", L"万", L"萵", L"莴", L"葉", L"叶", L"葒", L"荭", L"葤", L"荮", L"葦", L"苇", L"葯", L"药", L"葷", L"荤", L"蒓", L"莼", L"蒔", L"莳", L"蒞", L"莅", L"蒼", L"苍", L"蓀", L"荪", L"蓋", L"盖", L"蓮", L"莲", L"蓯", L"苁", L"蓴", L"莼", L"蓽", L"荜", L"蔔", L"卜", L"蔘", L"参", L"蔞", L"蒌", L"蔣", L"蒋", L"蔥", L"葱", L"蔦", L"茑", L"蔭", L"荫", L"蕁", L"荨", L"蕆", L"蒇", L"蕎", L"荞", L"蕒", L"荬", L"蕓", L"芸", L"蕕", L"莸", L"蕘", L"荛", L"蕢", L"蒉", L"蕩", L"荡", L"蕪", L"芜", L"蕭", L"萧", L"蕷", L"蓣", L"薀", L"蕰", L"薈", L"荟", L"薊", L"蓟", L"薌", L"芗", L"薑", L"姜", L"薔", L"蔷", L"薘", L"荙", L"薟", L"莶", L"薦", L"荐", L"薩", L"萨", L"薳", L"䓕", L"薴", L"苧", L"薺", L"荠", L"藍", L"蓝", L"藎", L"荩", L"藝", L"艺", L"藥", L"药", L"藪", L"薮", L"藴", L"蕴", L"藶", L"苈", L"藹", L"蔼", L"藺", L"蔺", L"蘄", L"蕲", L"蘆", L"芦", L"蘇", L"苏", L"蘊", L"蕴", L"蘋", L"苹", L"蘚", L"藓", L"蘞", L"蔹", L"蘢", L"茏", L"蘭", L"兰", L"蘺", L"蓠", L"蘿", L"萝", L"虆", L"蔂", L"處", L"处", L"虛", L"虚", L"虜", L"虏", L"號", L"号", L"虧", L"亏", L"虯", L"虬", L"蛺", L"蛱", L"蛻", L"蜕", L"蜆", L"蚬", L"蝕", L"蚀", L"蝟", L"猬", L"蝦", L"虾", L"蝸", L"蜗", L"螄", L"蛳", L"螞", L"蚂", L"螢", L"萤", L"螮", L"䗖", L"螻", L"蝼", L"螿", L"螀", L"蟄", L"蛰", L"蟈", L"蝈", L"蟎", L"螨", L"蟣", L"虮", L"蟬", L"蝉", L"蟯", L"蛲", L"蟲", L"虫", L"蟶", L"蛏", L"蟻", L"蚁", L"蠅", L"蝇", L"蠆", L"虿", L"蠍", L"蝎", L"蠐", L"蛴", L"蠑", L"蝾", L"蠟", L"蜡", L"蠣", L"蛎", L"蠨", L"蟏", L"蠱", L"蛊", L"蠶", L"蚕", L"蠻", L"蛮", L"衆", L"众", L"衊", L"蔑", L"術", L"术", L"衕", L"同", L"衚", L"胡", L"衛", L"卫", L"衝", L"冲", L"衹", L"只", L"袞", L"衮", L"裊", L"袅", L"裏", L"里", L"補", L"补", L"裝", L"装", L"裡", L"里", L"製", L"制", L"複", L"复", L"褌", L"裈", L"褘", L"袆", L"褲", L"裤", L"褳", L"裢", L"褸", L"褛", L"褻", L"亵", L"襀", L"𫌀", L"襆", L"幞", L"襇", L"裥", L"襏", L"袯", L"襖", L"袄", L"襝", L"裣", L"襠", L"裆", L"襤", L"褴", L"襪", L"袜", L"襬", L"䙓", L"襯", L"衬", L"襲", L"袭", L"見", L"见", L"覎", L"觃", L"規", L"规", L"覓", L"觅", L"視", L"视", L"覘", L"觇", L"覡", L"觋", L"覥", L"觍", L"覦", L"觎", L"親", L"亲", L"覬", L"觊", L"覯", L"觏", L"覲", L"觐", L"覷", L"觑", L"覺", L"觉", L"覼", L"𫌨", L"覽", L"览", L"覿", L"觌", L"觀", L"观", L"觴", L"觞", L"觶", L"觯", L"觸", L"触", L"訁", L"讠", L"訂", L"订", L"訃", L"讣", L"計", L"计", L"訊", L"讯", L"訌", L"讧", L"討", L"讨", L"訐", L"讦", L"訑", L"𫍙", L"訒", L"讱", L"訓", L"训", L"訕", L"讪", L"訖", L"讫", L"託", L"托", L"記", L"记", L"訛", L"讹", L"訝", L"讶", L"訟", L"讼", L"訢", L"䜣", L"訣", L"诀", L"訥", L"讷", L"訩", L"讻", L"訪", L"访", L"設", L"设", L"許", L"许", L"訴", L"诉", L"訶", L"诃", L"診", L"诊", L"註", L"注", L"詁", L"诂", L"詆", L"诋", L"詎", L"讵", L"詐", L"诈", L"詒", L"诒", L"詔", L"诏", L"評", L"评", L"詖", L"诐", L"詗", L"诇", L"詘", L"诎", L"詛", L"诅", L"詞", L"词", L"詠", L"咏", L"詡", L"诩", L"詢", L"询", L"詣", L"诣", L"試", L"试", L"詩", L"诗", L"詫", L"诧", L"詬", L"诟", L"詭", L"诡", L"詮", L"诠", L"詰", L"诘", L"話", L"话", L"該", L"该", L"詳", L"详", L"詵", L"诜", L"詼", L"诙", L"詿", L"诖", L"誄", L"诔", L"誅", L"诛", L"誆", L"诓", L"誇", L"夸", L"誌", L"志", L"認", L"认", L"誑", L"诳", L"誒", L"诶", L"誕", L"诞", L"誘", L"诱", L"誚", L"诮", L"語", L"语", L"誠", L"诚", L"誡", L"诫", L"誣", L"诬", L"誤", L"误", L"誥", L"诰", L"誦", L"诵", L"誨", L"诲", L"說", L"说", L"説", L"说", L"誰", L"谁", L"課", L"课", L"誶", L"谇", L"誹", L"诽", L"誼", L"谊", L"誾", L"訚", L"調", L"调", L"諂", L"谄", L"諄", L"谆", L"談", L"谈", L"諉", L"诿", L"請", L"请", L"諍", L"诤", L"諏", L"诹", L"諑", L"诼", L"諒", L"谅", L"論", L"论", L"諗", L"谂", L"諛", L"谀", L"諜", L"谍", L"諝", L"谞", L"諞", L"谝", L"諢", L"诨", L"諤", L"谔", L"諦", L"谛", L"諧", L"谐", L"諫", L"谏", L"諭", L"谕", L"諮", L"咨", L"諰", L"𫍰", L"諱", L"讳", L"諳", L"谙", L"諶", L"谌", L"諷", L"讽", L"諸", L"诸", L"諺", L"谚", L"諼", L"谖", L"諾", L"诺", L"謀", L"谋", L"謁", L"谒", L"謂", L"谓", L"謄", L"誊", L"謅", L"诌", L"謊", L"谎", L"謎", L"谜", L"謏", L"𫍲", L"謐", L"谧", L"謔", L"谑", L"謖", L"谡", L"謗", L"谤", L"謙", L"谦", L"謚", L"谥", L"講", L"讲", L"謝", L"谢", L"謠", L"谣", L"謡", L"谣", L"謨", L"谟", L"謫", L"谪", L"謬", L"谬", L"謭", L"谫", L"謳", L"讴", L"謹", L"谨", L"謾", L"谩", L"譅", L"䜧", L"證", L"证", L"譊", L"𫍢", L"譎", L"谲", L"譏", L"讥", L"譖", L"谮", L"識", L"识", L"譙", L"谯", L"譚", L"谭", L"譜", L"谱", L"譫", L"谵", L"譭", L"毁", L"譯", L"译", L"議", L"议", L"譴", L"谴", L"護", L"护", L"譸", L"诪", L"譽", L"誉", L"譾", L"谫", L"讀", L"读", L"變", L"变", L"讎", L"仇", L"讒", L"谗", L"讓", L"让", L"讕", L"谰", L"讖", L"谶", L"讚", L"赞", L"讜", L"谠", L"讞", L"谳", L"豈", L"岂", L"豎", L"竖", L"豐", L"丰", L"豔", L"艳", L"豬", L"猪", L"豶", L"豮", L"貓", L"猫", L"貙", L"䝙", L"貝", L"贝", L"貞", L"贞", L"貟", L"贠", L"負", L"负", L"財", L"财", L"貢", L"贡", L"貧", L"贫", L"貨", L"货", L"販", L"贩", L"貪", L"贪", L"貫", L"贯", L"責", L"责", L"貯", L"贮", L"貰", L"贳", L"貲", L"赀", L"貳", L"贰", L"貴", L"贵", L"貶", L"贬", L"買", L"买", L"貸", L"贷", L"貺", L"贶", L"費", L"费", L"貼", L"贴", L"貽", L"贻", L"貿", L"贸", L"賀", L"贺", L"賁", L"贲", L"賂", L"赂", L"賃", L"赁", L"賄", L"贿", L"賅", L"赅", L"資", L"资", L"賈", L"贾", L"賊", L"贼", L"賑", L"赈", L"賒", L"赊", L"賓", L"宾", L"賕", L"赇", L"賙", L"赒", L"賚", L"赉", L"賜", L"赐", L"賞", L"赏", L"賠", L"赔", L"賡", L"赓", L"賢", L"贤", L"賣", L"卖", L"賤", L"贱", L"賦", L"赋", L"賧", L"赕", L"質", L"质", L"賫", L"赍", L"賬", L"账", L"賭", L"赌", L"賰", L"䞐", L"賴", L"赖", L"賵", L"赗", L"賺", L"赚", L"賻", L"赙", L"購", L"购", L"賽", L"赛", L"賾", L"赜", L"贄", L"贽", L"贅", L"赘", L"贇", L"赟", L"贈", L"赠", L"贊", L"赞", L"贋", L"赝", L"贍", L"赡", L"贏", L"赢", L"贐", L"赆", L"贓", L"赃", L"贔", L"赑", L"贖", L"赎", L"贗", L"赝", L"贛", L"赣", L"贜", L"赃", L"赬", L"赪", L"趕", L"赶", L"趙", L"赵", L"趨", L"趋", L"趲", L"趱", L"跡", L"迹", L"踐", L"践", L"踴", L"踊", L"蹌", L"跄", L"蹕", L"跸", L"蹣", L"蹒", L"蹤", L"踪", L"蹺", L"跷", L"蹻", L"𫏋", L"躂", L"跶", L"躉", L"趸", L"躊", L"踌", L"躋", L"跻", L"躍", L"跃", L"躑", L"踯", L"躒", L"跞", L"躓", L"踬", L"躕", L"蹰", L"躚", L"跹", L"躡", L"蹑", L"躥", L"蹿", L"躦", L"躜", L"躪", L"躏", L"軀", L"躯", L"車", L"车", L"軋", L"轧", L"軌", L"轨", L"軍", L"军", L"軏", L"𫐄", L"軑", L"轪", L"軒", L"轩", L"軔", L"轫", L"軛", L"轭", L"軟", L"软", L"軤", L"轷", L"軨", L"𫐉", L"軫", L"轸", L"軲", L"轱", L"軸", L"轴", L"軹", L"轵", L"軺", L"轺", L"軻", L"轲", L"軼", L"轶", L"軾", L"轼", L"較", L"较", L"輅", L"辂", L"輇", L"辁", L"輈", L"辀", L"載", L"载", L"輊", L"轾", L"輒", L"辄", L"輓", L"挽", L"輔", L"辅", L"輕", L"轻", L"輗", L"𫐐", L"輛", L"辆", L"輜", L"辎", L"輝", L"辉", L"輞", L"辋", L"輟", L"辍", L"輥", L"辊", L"輦", L"辇", L"輩", L"辈", L"輪", L"轮", L"輬", L"辌", L"輮", L"𫐓", L"輯", L"辑", L"輳", L"辏", L"輸", L"输", L"輻", L"辐", L"輾", L"辗", L"輿", L"舆", L"轀", L"辒", L"轂", L"毂", L"轄", L"辖", L"轅", L"辕", L"轆", L"辘", L"轉", L"转", L"轍", L"辙", L"轎", L"轿", L"轔", L"辚", L"轟", L"轰", L"轡", L"辔", L"轢", L"轹", L"轣", L"𫐆", L"轤", L"轳", L"辦", L"办", L"辭", L"辞", L"辮", L"辫", L"辯", L"辩", L"農", L"农", L"迴", L"回", L"逕", L"迳", L"這", L"这", L"連", L"连", L"週", L"周", L"進", L"进", L"遊", L"游", L"運", L"运", L"過", L"过", L"達", L"达", L"違", L"违", L"遙", L"遥", L"遜", L"逊", L"遞", L"递", L"遠", L"远", L"遡", L"溯", L"適", L"适", L"遲", L"迟", L"遷", L"迁", L"選", L"选", L"遺", L"遗", L"遼", L"辽", L"邁", L"迈", L"還", L"还", L"邇", L"迩", L"邊", L"边", L"邏", L"逻", L"邐", L"逦", L"郟", L"郏", L"郵", L"邮", L"鄆", L"郓", L"鄉", L"乡", L"鄒", L"邹", L"鄔", L"邬", L"鄖", L"郧", L"鄧", L"邓", L"鄭", L"郑", L"鄰", L"邻", L"鄲", L"郸", L"鄴", L"邺", L"鄶", L"郐", L"鄺", L"邝", L"酇", L"酂", L"酈", L"郦", L"醖", L"酝", L"醜", L"丑", L"醞", L"酝", L"醣", L"糖", L"醫", L"医", L"醬", L"酱", L"醯", L"酰", L"醱", L"酦", L"釀", L"酿", L"釁", L"衅", L"釃", L"酾", L"釅", L"酽", L"釋", L"释", L"釐", L"厘", L"釒", L"钅", L"釓", L"钆", L"釔", L"钇", L"釕", L"钌", L"釗", L"钊", L"釘", L"钉", L"釙", L"钋", L"針", L"针", L"釣", L"钓", L"釤", L"钐", L"釧", L"钏", L"釩", L"钒", L"釳", L"𨰿", L"釵", L"钗", L"釷", L"钍", L"釹", L"钕", L"釺", L"钎", L"釾", L"䥺", L"鈀", L"钯", L"鈁", L"钫", L"鈃", L"钘", L"鈄", L"钭", L"鈇", L"𫓧", L"鈈", L"钚", L"鈉", L"钠", L"鈋", L"𨱂", L"鈍", L"钝", L"鈎", L"钩", L"鈐", L"钤", L"鈑", L"钣", L"鈒", L"钑", L"鈔", L"钞", L"鈕", L"钮", L"鈞", L"钧", L"鈠", L"𨱁", L"鈣", L"钙", L"鈥", L"钬", L"鈦", L"钛", L"鈧", L"钪", L"鈮", L"铌", L"鈯", L"𨱄", L"鈰", L"铈", L"鈲", L"𨱃", L"鈳", L"钶", L"鈴", L"铃", L"鈷", L"钴", L"鈸", L"钹", L"鈹", L"铍", L"鈺", L"钰", L"鈽", L"钸", L"鈾", L"铀", L"鈿", L"钿", L"鉀", L"钾", L"鉁", L"𨱅", L"鉅", L"钜", L"鉈", L"铊", L"鉉", L"铉", L"鉋", L"铇", L"鉍", L"铋", L"鉑", L"铂", L"鉕", L"钷", L"鉗", L"钳", L"鉚", L"铆", L"鉛", L"铅", L"鉞", L"钺", L"鉢", L"钵", L"鉤", L"钩", L"鉦", L"钲", L"鉬", L"钼", L"鉭", L"钽", L"鉶", L"铏", L"鉸", L"铰", L"鉺", L"铒", L"鉻", L"铬", L"鉿", L"铪", L"銀", L"银", L"銃", L"铳", L"銅", L"铜", L"銍", L"铚", L"銑", L"铣", L"銓", L"铨", L"銖", L"铢", L"銘", L"铭", L"銚", L"铫", L"銛", L"铦", L"銜", L"衔", L"銠", L"铑", L"銣", L"铷", L"銥", L"铱", L"銦", L"铟", L"銨", L"铵", L"銩", L"铥", L"銪", L"铕", L"銫", L"铯", L"銬", L"铐", L"銱", L"铞", L"銳", L"锐", L"銶", L"𨱇", L"銷", L"销", L"銹", L"锈", L"銻", L"锑", L"銼", L"锉", L"鋁", L"铝", L"鋃", L"锒", L"鋅", L"锌", L"鋇", L"钡", L"鋉", L"𨱈", L"鋌", L"铤", L"鋏", L"铗", L"鋒", L"锋", L"鋙", L"铻", L"鋝", L"锊", L"鋟", L"锓", L"鋣", L"铘", L"鋤", L"锄", L"鋥", L"锃", L"鋦", L"锔", L"鋨", L"锇", L"鋩", L"铓", L"鋪", L"铺", L"鋭", L"锐", L"鋮", L"铖", L"鋯", L"锆", L"鋰", L"锂", L"鋱", L"铽", L"鋶", L"锍", L"鋸", L"锯", L"鋼", L"钢", L"錁", L"锞", L"錂", L"𨱋", L"錄", L"录", L"錆", L"锖", L"錇", L"锫", L"錈", L"锩", L"錏", L"铔", L"錐", L"锥", L"錒", L"锕", L"錕", L"锟", L"錘", L"锤", L"錙", L"锱", L"錚", L"铮", L"錛", L"锛", L"錟", L"锬", L"錠", L"锭", L"錡", L"锜", L"錢", L"钱", L"錦", L"锦", L"錨", L"锚", L"錩", L"锠", L"錫", L"锡", L"錮", L"锢", L"錯", L"错", L"録", L"录", L"錳", L"锰", L"錶", L"表", L"錸", L"铼", L"鍀", L"锝", L"鍁", L"锨", L"鍃", L"锪", L"鍄", L"𨱉", L"鍆", L"钔", L"鍇", L"锴", L"鍈", L"锳", L"鍊", L"炼", L"鍋", L"锅", L"鍍", L"镀", L"鍔", L"锷", L"鍘", L"铡", L"鍚", L"钖", L"鍛", L"锻", L"鍠", L"锽", L"鍤", L"锸", L"鍥", L"锲", L"鍩", L"锘", L"鍬", L"锹", L"鍮", L"𨱎", L"鍰", L"锾", L"鍵", L"键", L"鍶", L"锶", L"鍺", L"锗", L"鍾", L"钟", L"鎂", L"镁", L"鎄", L"锿", L"鎇", L"镅", L"鎊", L"镑", L"鎌", L"镰", L"鎔", L"镕", L"鎖", L"锁", L"鎘", L"镉", L"鎚", L"锤", L"鎛", L"镈", L"鎝", L"𨱏", L"鎡", L"镃", L"鎢", L"钨", L"鎣", L"蓥", L"鎦", L"镏", L"鎧", L"铠", L"鎩", L"铩", L"鎪", L"锼", L"鎬", L"镐", L"鎭", L"鎮", L"鎮", L"镇", L"鎯", L"𨱍", L"鎰", L"镒", L"鎲", L"镋", L"鎳", L"镍", L"鎵", L"镓", L"鎷", L"𨰾", L"鎸", L"镌", L"鎿", L"镎", L"鏃", L"镞", L"鏆", L"𨱌", L"鏇", L"镟", L"鏈", L"链", L"鏉", L"𨱒", L"鏌", L"镆", L"鏍", L"镙", L"鏐", L"镠", L"鏑", L"镝", L"鏗", L"铿", L"鏘", L"锵", L"鏚", L"戚", L"鏜", L"镗", L"鏝", L"镘", L"鏞", L"镛", L"鏟", L"铲", L"鏡", L"镜", L"鏢", L"镖", L"鏤", L"镂", L"鏦", L"𫓩", L"鏨", L"錾", L"鏰", L"镚", L"鏵", L"铧", L"鏷", L"镤", L"鏹", L"镪", L"鏺", L"䥽", L"鏽", L"锈", L"鐃", L"铙", L"鐋", L"铴", L"鐍", L"𫔎", L"鐎", L"𨱓", L"鐏", L"𨱔", L"鐐", L"镣", L"鐒", L"铹", L"鐓", L"镦", L"鐔", L"镡", L"鐘", L"钟", L"鐙", L"镫", L"鐝", L"镢", L"鐠", L"镨", L"鐥", L"䦅", L"鐦", L"锎", L"鐧", L"锏", L"鐨", L"镄", L"鐫", L"镌", L"鐮", L"镰", L"鐯", L"䦃", L"鐲", L"镯", L"鐳", L"镭", L"鐵", L"铁", L"鐶", L"镮", L"鐸", L"铎", L"鐺", L"铛", L"鐿", L"镱", L"鑄", L"铸", L"鑊", L"镬", L"鑌", L"镔", L"鑑", L"鉴", L"鑒", L"鉴", L"鑔", L"镲", L"鑕", L"锧", L"鑞", L"镴", L"鑠", L"铄", L"鑣", L"镳", L"鑥", L"镥", L"鑭", L"镧", L"鑰", L"钥", L"鑱", L"镵", L"鑲", L"镶", L"鑷", L"镊", L"鑹", L"镩", L"鑼", L"锣", L"鑽", L"钻", L"鑾", L"銮", L"鑿", L"凿", L"钁", L"镢", L"镟", L"旋", L"長", L"长", L"門", L"门", L"閂", L"闩", L"閃", L"闪", L"閆", L"闫", L"閈", L"闬", L"閉", L"闭", L"開", L"开", L"閌", L"闶", L"閍", L"𨸂", L"閎", L"闳", L"閏", L"闰", L"閐", L"𨸃", L"閑", L"闲", L"閒", L"闲", L"間", L"间", L"閔", L"闵", L"閘", L"闸", L"閡", L"阂", L"閣", L"阁", L"閤", L"合", L"閥", L"阀", L"閨", L"闺", L"閩", L"闽", L"閫", L"阃", L"閬", L"阆", L"閭", L"闾", L"閱", L"阅", L"閲", L"阅", L"閶", L"阊", L"閹", L"阉", L"閻", L"阎", L"閼", L"阏", L"閽", L"阍", L"閾", L"阈", L"閿", L"阌", L"闃", L"阒", L"闆", L"板", L"闈", L"闱", L"闊", L"阔", L"闋", L"阕", L"闌", L"阑", L"闍", L"阇", L"闐", L"阗", L"闒", L"阘", L"闓", L"闿", L"闔", L"阖", L"闕", L"阙", L"闖", L"闯", L"關", L"关", L"闞", L"阚", L"闠", L"阓", L"闡", L"阐", L"闢", L"辟", L"闤", L"阛", L"闥", L"闼", L"陘", L"陉", L"陝", L"陕", L"陞", L"升", L"陣", L"阵", L"陰", L"阴", L"陳", L"陈", L"陸", L"陆", L"陽", L"阳", L"隉", L"陧", L"隊", L"队", L"階", L"阶", L"隕", L"陨", L"際", L"际", L"隨", L"随", L"險", L"险", L"隱", L"隐", L"隴", L"陇", L"隸", L"隶", L"隻", L"只", L"雋", L"隽", L"雖", L"虽", L"雙", L"双", L"雛", L"雏", L"雜", L"杂", L"雞", L"鸡", L"離", L"离", L"難", L"难", L"雲", L"云", L"電", L"电", L"霢", L"霡", L"霧", L"雾", L"霽", L"霁", L"靂", L"雳", L"靄", L"霭", L"靈", L"灵", L"靚", L"靓", L"靜", L"静", L"靦", L"腼", L"靨", L"靥", L"鞀", L"鼗", L"鞏", L"巩", L"鞝", L"绱", L"鞦", L"秋", L"鞽", L"鞒", L"韁", L"缰", L"韃", L"鞑", L"韆", L"千", L"韉", L"鞯", L"韋", L"韦", L"韌", L"韧", L"韍", L"韨", L"韓", L"韩", L"韙", L"韪", L"韜", L"韬", L"韝", L"鞲", L"韞", L"韫", L"韻", L"韵", L"響", L"响", L"頁", L"页", L"頂", L"顶", L"頃", L"顷", L"項", L"项", L"順", L"顺", L"頇", L"顸", L"須", L"须", L"頊", L"顼", L"頌", L"颂", L"頎", L"颀", L"頏", L"颃", L"預", L"预", L"頑", L"顽", L"頒", L"颁", L"頓", L"顿", L"頗", L"颇", L"領", L"领", L"頜", L"颌", L"頡", L"颉", L"頤", L"颐", L"頦", L"颏", L"頭", L"头", L"頮", L"颒", L"頰", L"颊", L"頲", L"颋", L"頴", L"颕", L"頷", L"颔", L"頸", L"颈", L"頹", L"颓", L"頻", L"频", L"頽", L"颓", L"顃", L"𩖖", L"顆", L"颗", L"題", L"题", L"額", L"额", L"顎", L"颚", L"顏", L"颜", L"顒", L"颙", L"顓", L"颛", L"顔", L"颜", L"願", L"愿", L"顙", L"颡", L"顛", L"颠", L"類", L"类", L"顢", L"颟", L"顥", L"颢", L"顧", L"顾", L"顫", L"颤", L"顬", L"颥", L"顯", L"显", L"顰", L"颦", L"顱", L"颅", L"顳", L"颞", L"顴", L"颧", L"風", L"风", L"颭", L"飐", L"颮", L"飑", L"颯", L"飒", L"颰", L"𩙥", L"颱", L"台", L"颳", L"刮", L"颶", L"飓", L"颷", L"𩙪", L"颸", L"飔", L"颺", L"飏", L"颻", L"飖", L"颼", L"飕", L"颾", L"𩙫", L"飀", L"飗", L"飄", L"飘", L"飆", L"飙", L"飈", L"飚", L"飛", L"飞", L"飠", L"饣", L"飢", L"饥", L"飣", L"饤", L"飥", L"饦", L"飩", L"饨", L"飪", L"饪", L"飫", L"饫", L"飭", L"饬", L"飯", L"饭", L"飱", L"飧", L"飲", L"饮", L"飴", L"饴", L"飼", L"饲", L"飽", L"饱", L"飾", L"饰", L"飿", L"饳", L"餃", L"饺", L"餄", L"饸", L"餅", L"饼", L"餉", L"饷", L"養", L"养", L"餌", L"饵", L"餎", L"饹", L"餏", L"饻", L"餑", L"饽", L"餒", L"馁", L"餓", L"饿", L"餔", L"𫗦", L"餕", L"馂", L"餖", L"饾", L"餗", L"𫗧", L"餘", L"余", L"餚", L"肴", L"餛", L"馄", L"餜", L"馃", L"餞", L"饯", L"餡", L"馅", L"餦", L"𫗠", L"館", L"馆", L"餭", L"𫗮", L"餱", L"糇", L"餳", L"饧", L"餵", L"喂", L"餶", L"馉", L"餷", L"馇", L"餸", L"𩠌", L"餺", L"馎", L"餼", L"饩", L"餾", L"馏", L"餿", L"馊", L"饁", L"馌", L"饃", L"馍", L"饅", L"馒", L"饈", L"馐", L"饉", L"馑", L"饊", L"馓", L"饋", L"馈", L"饌", L"馔", L"饑", L"饥", L"饒", L"饶", L"饗", L"飨", L"饘", L"𫗴", L"饜", L"餍", L"饞", L"馋", L"饢", L"馕", L"馬", L"马", L"馭", L"驭", L"馮", L"冯", L"馱", L"驮", L"馳", L"驰", L"馴", L"驯", L"馹", L"驲", L"駁", L"驳", L"駃", L"𫘝", L"駎", L"𩧨", L"駐", L"驻", L"駑", L"驽", L"駒", L"驹", L"駔", L"驵", L"駕", L"驾", L"駘", L"骀", L"駙", L"驸", L"駚", L"𩧫", L"駛", L"驶", L"駝", L"驼", L"駟", L"驷", L"駡", L"骂", L"駢", L"骈", L"駧", L"𩧲", L"駩", L"𩧴", L"駭", L"骇", L"駰", L"骃", L"駱", L"骆", L"駶", L"𩧺", L"駸", L"骎", L"駻", L"𫘣", L"駿", L"骏", L"騁", L"骋", L"騂", L"骍", L"騃", L"𫘤", L"騅", L"骓", L"騌", L"骔", L"騍", L"骒", L"騎", L"骑", L"騏", L"骐", L"騔", L"𩨀", L"騖", L"骛", L"騙", L"骗", L"騚", L"𩨊", L"騝", L"𩨃", L"騟", L"𩨈", L"騠", L"𫘨", L"騤", L"骙", L"騧", L"䯄", L"騪", L"𩨄", L"騫", L"骞", L"騭", L"骘", L"騮", L"骝", L"騰", L"腾", L"騶", L"驺", L"騷", L"骚", L"騸", L"骟", L"騾", L"骡", L"驀", L"蓦", L"驁", L"骜", L"驂", L"骖", L"驃", L"骠", L"驄", L"骢", L"驅", L"驱", L"驊", L"骅", L"驋", L"𩧯", L"驌", L"骕", L"驍", L"骁", L"驏", L"骣", L"驕", L"骄", L"驗", L"验", L"驚", L"惊", L"驛", L"驿", L"驟", L"骤", L"驢", L"驴", L"驤", L"骧", L"驥", L"骥", L"驦", L"骦", L"驪", L"骊", L"驫", L"骉", L"骯", L"肮", L"髏", L"髅", L"髒", L"脏", L"體", L"体", L"髕", L"髌", L"髖", L"髋", L"髮", L"发", L"鬆", L"松", L"鬍", L"胡", L"鬚", L"须", L"鬢", L"鬓", L"鬥", L"斗", L"鬧", L"闹", L"鬨", L"哄", L"鬩", L"阋", L"鬮", L"阄", L"鬱", L"郁", L"魎", L"魉", L"魘", L"魇", L"魚", L"鱼", L"魛", L"鱽", L"魟", L"𫚉", L"魢", L"鱾", L"魥", L"𩽹", L"魨", L"鲀", L"魯", L"鲁", L"魴", L"鲂", L"魷", L"鱿", L"魺", L"鲄", L"鮁", L"鲅", L"鮃", L"鲆", L"鮄", L"𫚒", L"鮊", L"鲌", L"鮋", L"鲉", L"鮍", L"鲏", L"鮎", L"鲇", L"鮐", L"鲐", L"鮑", L"鲍", L"鮒", L"鲋", L"鮓", L"鲊", L"鮕", L"𩾀", L"鮚", L"鲒", L"鮜", L"鲘", L"鮝", L"鲞", L"鮞", L"鲕", L"鮟", L"𩽾", L"鮣", L"䲟", L"鮦", L"鲖", L"鮪", L"鲔", L"鮫", L"鲛", L"鮭", L"鲑", L"鮮", L"鲜", L"鮰", L"𫚔", L"鮳", L"鲓", L"鮶", L"鲪", L"鮸", L"𩾃", L"鮺", L"鲝", L"鯀", L"鲧", L"鯁", L"鲠", L"鯄", L"𩾁", L"鯆", L"𫚙", L"鯇", L"鲩", L"鯉", L"鲤", L"鯊", L"鲨", L"鯒", L"鲬", L"鯔", L"鲻", L"鯕", L"鲯", L"鯖", L"鲭", L"鯗", L"鲞", L"鯛", L"鲷", L"鯝", L"鲴", L"鯡", L"鲱", L"鯢", L"鲵", L"鯤", L"鲲", L"鯧", L"鲳", L"鯨", L"鲸", L"鯪", L"鲮", L"鯫", L"鲰", L"鯰", L"鲇", L"鯱", L"𩾇", L"鯴", L"鲺", L"鯶", L"𩽼", L"鯷", L"鳀", L"鯽", L"鲫", L"鯿", L"鳊", L"鰁", L"鳈", L"鰂", L"鲗", L"鰃", L"鳂", L"鰆", L"䲠", L"鰈", L"鲽", L"鰉", L"鳇", L"鰌", L"䲡", L"鰍", L"鳅", L"鰏", L"鲾", L"鰐", L"鳄", L"鰒", L"鳆", L"鰓", L"鳃", L"鰜", L"鳒", L"鰟", L"鳑", L"鰠", L"鳋", L"鰣", L"鲥", L"鰤", L"𫚕", L"鰥", L"鳏", L"鰧", L"䲢", L"鰨", L"鳎", L"鰩", L"鳐", L"鰭", L"鳍", L"鰮", L"鳁", L"鰱", L"鲢", L"鰲", L"鳌", L"鰳", L"鳓", L"鰵", L"鳘", L"鰷", L"鲦", L"鰹", L"鲣", L"鰺", L"鲹", L"鰻", L"鳗", L"鰼", L"鳛", L"鰾", L"鳔", L"鱂", L"鳉", L"鱅", L"鳙", L"鱇", L"𩾌", L"鱈", L"鳕", L"鱉", L"鳖", L"鱒", L"鳟", L"鱔", L"鳝", L"鱖", L"鳜", L"鱗", L"鳞", L"鱘", L"鲟", L"鱝", L"鲼", L"鱟", L"鲎", L"鱠", L"鲙", L"鱣", L"鳣", L"鱤", L"鳡", L"鱧", L"鳢", L"鱨", L"鲿", L"鱭", L"鲚", L"鱮", L"𫚈", L"鱯", L"鳠", L"鱷", L"鳄", L"鱸", L"鲈", L"鱺", L"鲡", L"鳥", L"鸟", L"鳧", L"凫", L"鳩", L"鸠", L"鳬", L"凫", L"鳲", L"鸤", L"鳳", L"凤", L"鳴", L"鸣", L"鳶", L"鸢", L"鳷", L"𫛛", L"鳼", L"𪉃", L"鳾", L"䴓", L"鴃", L"𫛞", L"鴆", L"鸩", L"鴇", L"鸨", L"鴉", L"鸦", L"鴒", L"鸰", L"鴕", L"鸵", L"鴗", L"𫁡", L"鴛", L"鸳", L"鴜", L"𪉈", L"鴝", L"鸲", L"鴞", L"鸮", L"鴟", L"鸱", L"鴣", L"鸪", L"鴦", L"鸯", L"鴨", L"鸭", L"鴯", L"鸸", L"鴰", L"鸹", L"鴲", L"𪉆", L"鴴", L"鸻", L"鴷", L"䴕", L"鴻", L"鸿", L"鴿", L"鸽", L"鵁", L"䴔", L"鵂", L"鸺", L"鵃", L"鸼", L"鵐", L"鹀", L"鵑", L"鹃", L"鵒", L"鹆", L"鵓", L"鹁", L"鵚", L"𪉍", L"鵜", L"鹈", L"鵝", L"鹅", L"鵠", L"鹄", L"鵡", L"鹉", L"鵪", L"鹌", L"鵬", L"鹏", L"鵮", L"鹐", L"鵯", L"鹎", L"鵰", L"雕", L"鵲", L"鹊", L"鵷", L"鹓", L"鵾", L"鹍", L"鶄", L"䴖", L"鶇", L"鸫", L"鶉", L"鹑", L"鶊", L"鹒", L"鶒", L"𫛶", L"鶓", L"鹋", L"鶖", L"鹙", L"鶗", L"𫛸", L"鶘", L"鹕", L"鶚", L"鹗", L"鶡", L"鹖", L"鶥", L"鹛", L"鶩", L"鹜", L"鶪", L"䴗", L"鶬", L"鸧", L"鶯", L"莺", L"鶲", L"鹟", L"鶴", L"鹤", L"鶹", L"鹠", L"鶺", L"鹡", L"鶻", L"鹘", L"鶼", L"鹣", L"鶿", L"鹚", L"鷀", L"鹚", L"鷁", L"鹢", L"鷂", L"鹞", L"鷄", L"鸡", L"鷈", L"䴘", L"鷊", L"鹝", L"鷓", L"鹧", L"鷔", L"𪉑", L"鷖", L"鹥", L"鷗", L"鸥", L"鷙", L"鸷", L"鷚", L"鹨", L"鷥", L"鸶", L"鷦", L"鹪", L"鷨", L"𪉊", L"鷫", L"鹔", L"鷯", L"鹩", L"鷲", L"鹫", L"鷳", L"鹇", L"鷸", L"鹬", L"鷹", L"鹰", L"鷺", L"鹭", L"鷽", L"鸴", L"鷿", L"䴙", L"鸂", L"㶉", L"鸇", L"鹯", L"鸋", L"𫛢", L"鸌", L"鹱", L"鸏", L"鹲", L"鸕", L"鸬", L"鸘", L"鹴", L"鸚", L"鹦", L"鸛", L"鹳", L"鸝", L"鹂", L"鸞", L"鸾", L"鹵", L"卤", L"鹹", L"咸", L"鹺", L"鹾", L"鹼", L"碱", L"鹽", L"盐", L"麗", L"丽", L"麥", L"麦", L"麨", L"𪎊", L"麩", L"麸", L"麪", L"面", L"麫", L"面", L"麯", L"曲", L"麲", L"𪎉", L"麳", L"𪎌", L"麴", L"曲", L"麵", L"面", L"麼", L"么", L"麽", L"么", L"黃", L"黄", L"黌", L"黉", L"點", L"点", L"黨", L"党", L"黲", L"黪", L"黴", L"霉", L"黶", L"黡", L"黷", L"黩", L"黽", L"黾", L"黿", L"鼋", L"鼉", L"鼍", L"鼕", L"冬", L"鼴", L"鼹", L"齇", L"齄", L"齊", L"齐", L"齋", L"斋", L"齎", L"赍", L"齏", L"齑", L"齒", L"齿", L"齔", L"龀", L"齕", L"龁", L"齗", L"龂", L"齙", L"龅", L"齜", L"龇", L"齟", L"龃", L"齠", L"龆", L"齡", L"龄", L"齣", L"出", L"齦", L"龈", L"齪", L"龊", L"齬", L"龉", L"齲", L"龋", L"齶", L"腭", L"齷", L"龌", L"龍", L"龙", L"龎", L"厐", L"龐", L"庞", L"龔", L"龚", L"龕", L"龛", L"龜", L"龟", L"𡞵", L"㛟", L"𡠹", L"㛿", L"𡢃", L"㛠", L"𡻕", L"岁", L"𤪺", L"㻘", L"𤫩", L"㻏", L"𦪙", L"䑽", L"𧜵", L"䙊", L"𧝞", L"䘛", L"𧦧", L"𫍟", L"𧩙", L"䜥", L"𧵳", L"䞌", L"𨋢", L"䢂", L"𨥛", L"𨱀", L"𨦫", L"䦀", L"𨧜", L"䦁", L"𨧱", L"𨱊", L"𨫒", L"𨱐", L"𨮂", L"𨱕", L"𨯅", L"䥿", L"𩎢", L"𩏾", L"𩏪", L"𩏽", L"𩓣", L"𩖕", L"𩗀", L"𩙦", L"𩗡", L"𩙧", L"𩘀", L"𩙩", L"𩘝", L"𩙭", L"𩘹", L"𩙨", L"𩘺", L"𩙬", L"𩙈", L"𩙰", L"𩜦", L"𩠆", L"𩝔", L"𩠋", L"𩞯", L"䭪", L"𩟐", L"𩠅", L"𩡺", L"𩧦", L"𩢡", L"𩧬", L"𩢴", L"𩧵", L"𩢸", L"𩧳", L"𩢾", L"𩧮", L"𩣏", L"𩧶", L"𩣑", L"䯃", L"𩣵", L"𩧻", L"𩣺", L"𩧼", L"𩤊", L"𩧩", L"𩤙", L"𩨆", L"𩤲", L"𩨉", L"𩤸", L"𩨅", L"𩥄", L"𩨋", L"𩥇", L"𩨍", L"𩥉", L"𩧱", L"𩥑", L"𩨌", L"𩧆", L"𩨐", L"𩵩", L"𩽺", L"𩵹", L"𩽻", L"𩶘", L"䲞", L"𩶰", L"𩽿", L"𩶱", L"𩽽", L"𩷰", L"𩾄", L"𩸃", L"𩾅", L"𩸦", L"𩾆", L"𩽇", L"𩾎", L"𩿪", L"𪉄", L"𪀦", L"𪉅", L"𪀾", L"𪉋", L"𪁈", L"𪉉", L"𪁖", L"𪉌", L"𪂆", L"𪉎", L"𪃍", L"𪉐", L"𪃏", L"𪉏", L"𪄆", L"𪉔", L"𪄕", L"𪉒", L"𪇳", L"𪉕", L"𪘀", L"𪚏", L"𪘯", L"𪚐", L"𫚒", L"軿", L"《易乾'", L"《易乾", L"不著痕跡'", L"不着痕迹", L"不著邊際'", L"不着边际", L"與著'", L"与着", L"與著書'", L"与著书", L"與著作'", L"与著作", L"與著名'", L"与著名", L"與著錄'", L"与著录", L"與著稱'", L"与著称", L"與著者'", L"与著者", L"與著述'", L"与著述", L"丑著'", L"丑着", L"丑著書'", L"丑著书", L"丑著作'", L"丑著作", L"丑著名'", L"丑著名", L"丑著錄'", L"丑著录", L"丑著稱'", L"丑著称", L"丑著者'", L"丑著者", L"丑著述'", L"丑著述", L"專著'", L"专著", L"臨著'", L"临着", L"臨著書'", L"临著书", L"臨著作'", L"临著作", L"臨著名'", L"临著名", L"臨著錄'", L"临著录", L"臨著稱'", L"临著称", L"臨著者'", L"临著者", L"臨著述'", L"临著述", L"麗著'", L"丽着", L"麗著書'", L"丽著书", L"麗著作'", L"丽著作", L"麗著名'", L"丽著名", L"麗著錄'", L"丽著录", L"麗著稱'", L"丽著称", L"麗著者'", L"丽著者", L"麗著述'", L"丽著述", L"樂著'", L"乐着", L"樂著書'", L"乐著书", L"樂著作'", L"乐著作", L"樂著名'", L"乐著名", L"樂著錄'", L"乐著录", L"樂著稱'", L"乐著称", L"樂著者'", L"乐著者", L"樂著述'", L"乐著述", L"乘著'", L"乘着", L"乘著書'", L"乘著书", L"乘著作'", L"乘著作", L"乘著名'", L"乘著名", L"乘著錄'", L"乘著录", L"乘著稱'", L"乘著称", L"乘著者'", L"乘著者", L"乘著述'", L"乘著述", L"乾上乾下'", L"乾上乾下", L"乾為天'", L"乾为天", L"乾為陽'", L"乾为阳", L"乾九'", L"乾九", L"乾乾'", L"乾乾", L"乾亨'", L"乾亨", L"乾儀'", L"乾仪", L"乾仪'", L"乾仪", L"乾位'", L"乾位", L"乾健'", L"乾健", L"乾健也'", L"乾健也", L"乾元'", L"乾元", L"乾光'", L"乾光", L"乾兴'", L"乾兴", L"乾興'", L"乾兴", L"乾冈'", L"乾冈", L"乾岡'", L"乾冈", L"乾劉'", L"乾刘", L"乾刘'", L"乾刘", L"乾剛'", L"乾刚", L"乾刚'", L"乾刚", L"乾務'", L"乾务", L"乾务'", L"乾务", L"乾化'", L"乾化", L"乾卦'", L"乾卦", L"乾县'", L"乾县", L"乾縣'", L"乾县", L"乾台'", L"乾台", L"乾吉'", L"乾吉", L"乾啟'", L"乾启", L"乾启'", L"乾启", L"乾命'", L"乾命", L"乾和'", L"乾和", L"乾嘉'", L"乾嘉", L"乾圖'", L"乾图", L"乾图'", L"乾图", L"乾坤'", L"乾坤", L"乾城'", L"乾城", L"乾基'", L"乾基", L"乾天也'", L"乾天也", L"乾始'", L"乾始", L"乾姓'", L"乾姓", L"乾寧'", L"乾宁", L"乾宁'", L"乾宁", L"乾宅'", L"乾宅", L"乾宇'", L"乾宇", L"乾安'", L"乾安", L"乾定'", L"乾定", L"乾封'", L"乾封", L"乾居'", L"乾居", L"乾崗'", L"乾岗", L"乾岗'", L"乾岗", L"乾巛'", L"乾巛", L"乾州'", L"乾州", L"乾式'", L"乾式", L"乾錄'", L"乾录", L"乾录'", L"乾录", L"乾律'", L"乾律", L"乾德'", L"乾德", L"乾心'", L"乾心", L"乾忠'", L"乾忠", L"乾文'", L"乾文", L"乾斷'", L"乾断", L"乾断'", L"乾断", L"乾方'", L"乾方", L"乾施'", L"乾施", L"乾旦'", L"乾旦", L"乾明'", L"乾明", L"乾昧'", L"乾昧", L"乾暉'", L"乾晖", L"乾晖'", L"乾晖", L"乾景'", L"乾景", L"乾晷'", L"乾晷", L"乾曜'", L"乾曜", L"乾构'", L"乾构", L"乾構'", L"乾构", L"乾枢'", L"乾枢", L"乾樞'", L"乾枢", L"乾栋'", L"乾栋", L"乾棟'", L"乾栋", L"乾步'", L"乾步", L"乾氏'", L"乾氏", L"乾沓和'", L"乾沓和", L"乾沓婆'", L"乾沓婆", L"乾泉'", L"乾泉", L"乾淳'", L"乾淳", L"乾清宮'", L"乾清宫", L"乾清宫'", L"乾清宫", L"乾渥'", L"乾渥", L"乾靈'", L"乾灵", L"乾灵'", L"乾灵", L"乾男'", L"乾男", L"乾皋'", L"乾皋", L"乾盛世'", L"乾盛世", L"乾矢'", L"乾矢", L"乾祐'", L"乾祐", L"乾穹'", L"乾穹", L"乾竇'", L"乾窦", L"乾窦'", L"乾窦", L"乾竺'", L"乾竺", L"乾篤'", L"乾笃", L"乾笃'", L"乾笃", L"乾符'", L"乾符", L"乾策'", L"乾策", L"乾精'", L"乾精", L"乾紅'", L"乾红", L"乾红'", L"乾红", L"乾綱'", L"乾纲", L"乾纲'", L"乾纲", L"乾纽'", L"乾纽", L"乾紐'", L"乾纽", L"乾絡'", L"乾络", L"乾络'", L"乾络", L"乾統'", L"乾统", L"乾统'", L"乾统", L"乾維'", L"乾维", L"乾维'", L"乾维", L"乾羅'", L"乾罗", L"乾罗'", L"乾罗", L"乾花'", L"乾花", L"乾蔭'", L"乾荫", L"乾荫'", L"乾荫", L"乾行'", L"乾行", L"乾衡'", L"乾衡", L"乾覆'", L"乾覆", L"乾象'", L"乾象", L"乾象歷'", L"乾象历", L"乾象历'", L"乾象历", L"乾贞'", L"乾贞", L"乾貞'", L"乾贞", L"乾貺'", L"乾贶", L"乾贶'", L"乾贶", L"乾车'", L"乾车", L"乾車'", L"乾车", L"乾轴'", L"乾轴", L"乾軸'", L"乾轴", L"乾通'", L"乾通", L"乾造'", L"乾造", L"乾道'", L"乾道", L"乾鑒'", L"乾鉴", L"乾鉴'", L"乾鉴", L"乾钧'", L"乾钧", L"乾鈞'", L"乾钧", L"乾闼'", L"乾闼", L"乾闥'", L"乾闼", L"乾陀'", L"乾陀", L"乾陵'", L"乾陵", L"乾隆'", L"乾隆", L"乾音'", L"乾音", L"乾顾'", L"乾顾", L"乾顧'", L"乾顾", L"乾风'", L"乾风", L"乾風'", L"乾风", L"乾首'", L"乾首", L"乾馬'", L"乾马", L"乾马'", L"乾马", L"乾鵠'", L"乾鹄", L"乾鹄'", L"乾鹄", L"乾鵲'", L"乾鹊", L"乾鹊'", L"乾鹊", L"乾龍'", L"乾龙", L"乾龙'", L"乾龙", L"乾，健也'", L"乾，健也", L"乾，天也'", L"乾，天也", L"爭著'", L"争着", L"爭著書'", L"争著书", L"爭著作'", L"争著作", L"爭著名'", L"争著名", L"爭著錄'", L"争著录", L"爭著稱'", L"争著称", L"爭著者'", L"争著者", L"爭著述'", L"争著述", L"五箇山'", L"五箇山", L"亮著'", L"亮着", L"亮著書'", L"亮著书", L"亮著作'", L"亮著作", L"亮著名'", L"亮著名", L"亮著錄'", L"亮著录", L"亮著稱'", L"亮著称", L"亮著者'", L"亮著者", L"亮著述'", L"亮著述", L"仗著'", L"仗着", L"仗著書'", L"仗著书", L"仗著作'", L"仗著作", L"仗著名'", L"仗著名", L"仗著錄'", L"仗著录", L"仗著稱'", L"仗著称", L"仗著者'", L"仗著者", L"仗著述'", L"仗著述", L"代表著'", L"代表着", L"代表著書'", L"代表著书", L"代表著作'", L"代表著作", L"代表著名'", L"代表著名", L"代表著錄'", L"代表著录", L"代表著稱'", L"代表著称", L"代表著者'", L"代表著者", L"代表著述'", L"代表著述", L"以微知著'", L"以微知著", L"仰屋著書'", L"仰屋著书", L"彷彿'", L"仿佛", L"夥計'", L"伙计", L"傳著'", L"传着", L"傳著書'", L"传著书", L"傳著作'", L"传著作", L"傳著名'", L"传著名", L"傳著錄'", L"传著录", L"傳著稱'", L"传著称", L"傳著者'", L"传著者", L"傳著述'", L"传著述", L"伴著'", L"伴着", L"伴著書'", L"伴著书", L"伴著作'", L"伴著作", L"伴著名'", L"伴著名", L"伴著錄'", L"伴著录", L"伴著稱'", L"伴著称", L"伴著者'", L"伴著者", L"伴著述'", L"伴著述", L"低著'", L"低着", L"低著書'", L"低著书", L"低著作'", L"低著作", L"低著名'", L"低著名", L"低著錄'", L"低著录", L"低著稱'", L"低著称", L"低著者'", L"低著者", L"低著述'", L"低著述", L"住著'", L"住着", L"住著書'", L"住著书", L"住著作'", L"住著作", L"住著名'", L"住著名", L"住著錄'", L"住著录", L"住著稱'", L"住著称", L"住著者'", L"住著者", L"住著述'", L"住著述", L"佛頭著糞'", L"佛头著粪", L"侏儸紀'", L"侏罗纪", L"側著'", L"侧着", L"側著書'", L"侧著书", L"側著作'", L"侧著作", L"側著名'", L"侧著名", L"側著錄'", L"侧著录", L"側著稱'", L"侧著称", L"側著者'", L"侧著者", L"側著述'", L"侧著述", L"保護著'", L"保护着", L"保障著'", L"保障着", L"保障著書'", L"保障著书", L"保障著作'", L"保障著作", L"保障著名'", L"保障著名", L"保障著錄'", L"保障著录", L"保障著稱'", L"保障著称", L"保障著者'", L"保障著者", L"保障著述'", L"保障著述", L"信著'", L"信着", L"信著書'", L"信著书", L"信著作'", L"信著作", L"信著名'", L"信著名", L"信著錄'", L"信著录", L"信著稱'", L"信著称", L"信著者'", L"信著者", L"信著述'", L"信著述", L"修鍊'", L"修炼", L"候著'", L"候着", L"候著書'", L"候著书", L"候著作'", L"候著作", L"候著名'", L"候著名", L"候著錄'", L"候著录", L"候著稱'", L"候著称", L"候著者'", L"候著者", L"候著述'", L"候著述", L"藉助'", L"借助", L"藉口'", L"借口", L"藉手'", L"借手", L"藉故'", L"借故", L"藉機'", L"借机", L"藉此'", L"借此", L"藉由'", L"借由", L"借著'", L"借着", L"藉着'", L"借着", L"藉著'", L"借着", L"藉端'", L"借端", L"借著書'", L"借著书", L"借著作'", L"借著作", L"借著名'", L"借著名", L"借著錄'", L"借著录", L"借著稱'", L"借著称", L"借著者'", L"借著者", L"借著述'", L"借著述", L"藉詞'", L"借词", L"做著'", L"做着", L"做著書'", L"做著书", L"做著作'", L"做著作", L"做著名'", L"做著名", L"做著錄'", L"做著录", L"做著稱'", L"做著称", L"做著者'", L"做著者", L"做著述'", L"做著述", L"偷著'", L"偷着", L"偷著書'", L"偷著书", L"偷著作'", L"偷著作", L"偷著名'", L"偷著名", L"偷著錄'", L"偷著录", L"偷著稱'", L"偷著称", L"偷著者'", L"偷著者", L"偷著述'", L"偷著述", L"傢俬'", L"傢俬", L"光著'", L"光着", L"光著書'", L"光著书", L"光著作'", L"光著作", L"光著名'", L"光著名", L"光著錄'", L"光著录", L"光著稱'", L"光著称", L"光著者'", L"光著者", L"光著述'", L"光著述", L"關著'", L"关着", L"關著書'", L"关著书", L"關著作'", L"关著作", L"關著名'", L"关著名", L"關著錄'", L"关著录", L"關著稱'", L"关著称", L"關著者'", L"关著者", L"關著述'", L"关著述", L"冀著'", L"冀着", L"冀著書'", L"冀著书", L"冀著作'", L"冀著作", L"冀著名'", L"冀著名", L"冀著錄'", L"冀著录", L"冀著稱'", L"冀著称", L"冀著者'", L"冀著者", L"冀著述'", L"冀著述", L"冒著'", L"冒着", L"冒著書'", L"冒著书", L"冒著作'", L"冒著作", L"冒著名'", L"冒著名", L"冒著錄'", L"冒著录", L"冒著稱'", L"冒著称", L"冒著者'", L"冒著者", L"冒著述'", L"冒著述", L"寫著'", L"写着", L"寫著書'", L"写著书", L"寫著作'", L"写著作", L"寫著名'", L"写著名", L"寫著錄'", L"写著录", L"寫著稱'", L"写著称", L"寫著者'", L"写著者", L"寫著述'", L"写著述", L"涼著'", L"凉着", L"涼著書'", L"凉著书", L"涼著作'", L"凉著作", L"涼著名'", L"凉著名", L"涼著錄'", L"凉著录", L"涼著稱'", L"凉著称", L"涼著者'", L"凉著者", L"涼著述'", L"凉著述", L"憑藉'", L"凭借", L"制著'", L"制着", L"制著書'", L"制著书", L"制著作'", L"制著作", L"制著名'", L"制著名", L"制著錄'", L"制著录", L"制著稱'", L"制著称", L"制著者'", L"制著者", L"制著述'", L"制著述", L"刻著'", L"刻着", L"刻著書'", L"刻著书", L"刻著作'", L"刻著作", L"刻著名'", L"刻著名", L"刻著錄'", L"刻著录", L"刻著稱'", L"刻著称", L"刻著者'", L"刻著者", L"刻著述'", L"刻著述", L"辦著'", L"办着", L"辦著書'", L"办著书", L"辦著作'", L"办著作", L"辦著名'", L"办著名", L"辦著錄'", L"办著录", L"辦著稱'", L"办著称", L"辦著者'", L"办著者", L"辦著述'", L"办著述", L"動著'", L"动着", L"動著書'", L"动著书", L"動著作'", L"动著作", L"動著名'", L"动著名", L"動著錄'", L"动著录", L"動著稱'", L"动著称", L"動著者'", L"动著者", L"動著述'", L"动著述", L"努力著'", L"努力着", L"努力著書'", L"努力著书", L"努力著作'", L"努力著作", L"努力著名'", L"努力著名", L"努力著錄'", L"努力著录", L"努力著稱'", L"努力著称", L"努力著者'", L"努力著者", L"努力著述'", L"努力著述", L"努著'", L"努着", L"努著書'", L"努著书", L"努著作'", L"努著作", L"努著名'", L"努著名", L"努著錄'", L"努著录", L"努著稱'", L"努著称", L"努著者'", L"努著者", L"努著述'", L"努著述", L"卓著'", L"卓著", L"印著'", L"印着", L"印著書'", L"印著书", L"印著作'", L"印著作", L"印著名'", L"印著名", L"印著錄'", L"印著录", L"印著稱'", L"印著称", L"印著者'", L"印著者", L"印著述'", L"印著述", L"卷舌'", L"卷舌", L"壓著'", L"压着", L"壓著書'", L"压著书", L"壓著作'", L"压著作", L"壓著名'", L"压著名", L"壓著錄'", L"压著录", L"壓著稱'", L"压著称", L"壓著者'", L"压著者", L"壓著述'", L"压著述", L"原著'", L"原著", L"去著'", L"去着", L"去著書'", L"去著书", L"去著作'", L"去著作", L"去著名'", L"去著名", L"去著錄'", L"去著录", L"去著稱'", L"去著称", L"去著者'", L"去著者", L"去著述'", L"去著述", L"反反覆覆'", L"反反复复", L"反覆'", L"反复", L"受著'", L"受着", L"受著書'", L"受著书", L"受著作'", L"受著作", L"受著名'", L"受著名", L"受著錄'", L"受著录", L"受著稱'", L"受著称", L"受著者'", L"受著者", L"受著述'", L"受著述", L"變著'", L"变着", L"變著書'", L"变著书", L"變著作'", L"变著作", L"變著名'", L"变著名", L"變著錄'", L"变著录", L"變著稱'", L"变著称", L"變著者'", L"变著者", L"變著述'", L"变著述", L"叫著'", L"叫着", L"叫著書'", L"叫著书", L"叫著作'", L"叫著作", L"叫著名'", L"叫著名", L"叫著錄'", L"叫著录", L"叫著稱'", L"叫著称", L"叫著者'", L"叫著者", L"叫著述'", L"叫著述", L"可穿著'", L"可穿著", L"叱吒'", L"叱吒", L"吃衣著飯'", L"吃衣著饭", L"合著'", L"合著", L"名著'", L"名著", L"向著'", L"向着", L"向著書'", L"向著书", L"向著作'", L"向著作", L"向著名'", L"向著名", L"向著錄'", L"向著录", L"向著稱'", L"向著称", L"向著者'", L"向著者", L"向著述'", L"向著述", L"含著'", L"含着", L"含著書'", L"含著书", L"含著作'", L"含著作", L"含著名'", L"含著名", L"含著錄'", L"含著录", L"含著稱'", L"含著称", L"含著者'", L"含著者", L"含著述'", L"含著述", L"聽著'", L"听着", L"聽著書'", L"听著书", L"聽著作'", L"听著作", L"聽著名'", L"听著名", L"聽著錄'", L"听著录", L"聽著稱'", L"听著称", L"聽著者'", L"听著者", L"聽著述'", L"听著述", L"吴其濬'", L"吴其濬", L"吳其濬'", L"吴其濬", L"吹著'", L"吹着", L"吹著書'", L"吹著书", L"吹著作'", L"吹著作", L"吹著名'", L"吹著名", L"吹著錄'", L"吹著录", L"吹著稱'", L"吹著称", L"吹著者'", L"吹著者", L"吹著述'", L"吹著述", L"周易乾'", L"周易乾", L"味著'", L"味着", L"味著書'", L"味著书", L"味著作'", L"味著作", L"味著名'", L"味著名", L"味著錄'", L"味著录", L"味著稱'", L"味著称", L"味著者'", L"味著者", L"味著述'", L"味著述", L"呼幺喝六'", L"呼幺喝六", L"響著'", L"响着", L"響著書'", L"响著书", L"響著作'", L"响著作", L"響著名'", L"响著名", L"響著錄'", L"响著录", L"響著稱'", L"响著称", L"響著者'", L"响著者", L"響著述'", L"响著述", L"哪吒'", L"哪吒", L"哭著'", L"哭着", L"哭著書'", L"哭著书", L"哭著作'", L"哭著作", L"哭著名'", L"哭著名", L"哭著錄'", L"哭著录", L"哭著稱'", L"哭著称", L"哭著者'", L"哭著者", L"哭著述'", L"哭著述", L"唱著'", L"唱着", L"唱著書'", L"唱著书", L"唱著作'", L"唱著作", L"唱著名'", L"唱著名", L"唱著錄'", L"唱著录", L"唱著稱'", L"唱著称", L"唱著者'", L"唱著者", L"唱著述'", L"唱著述", L"喝著'", L"喝着", L"喝著書'", L"喝著书", L"喝著作'", L"喝著作", L"喝著名'", L"喝著名", L"喝著錄'", L"喝著录", L"喝著稱'", L"喝著称", L"喝著者'", L"喝著者", L"喝著述'", L"喝著述", L"嚷著'", L"嚷着", L"嚷著書'", L"嚷著书", L"嚷著作'", L"嚷著作", L"嚷著名'", L"嚷著名", L"嚷著錄'", L"嚷著录", L"嚷著稱'", L"嚷著称", L"嚷著者'", L"嚷著者", L"嚷著述'", L"嚷著述", L"回覆'", L"回复", L"因著'", L"因着", L"因著〈'", L"因著〈", L"因著《'", L"因著《", L"因著書'", L"因著书", L"因著作'", L"因著作", L"因著名'", L"因著名", L"因著錄'", L"因著录", L"因著稱'", L"因著称", L"因著者'", L"因著者", L"因著述'", L"因著述", L"困著'", L"困着", L"困著書'", L"困著书", L"困著作'", L"困著作", L"困著名'", L"困著名", L"困著錄'", L"困著录", L"困著稱'", L"困著称", L"困著者'", L"困著者", L"困著述'", L"困著述", L"圍著'", L"围着", L"圍著書'", L"围著书", L"圍著作'", L"围著作", L"圍著名'", L"围著名", L"圍著錄'", L"围著录", L"圍著稱'", L"围著称", L"圍著者'", L"围著者", L"圍著述'", L"围著述", L"土著'", L"土著", L"在著'", L"在着", L"在著書'", L"在著书", L"在著作'", L"在著作", L"在著名'", L"在著名", L"在著錄'", L"在著录", L"在著稱'", L"在著称", L"在著者'", L"在著者", L"在著述'", L"在著述", L"坐著'", L"坐着", L"坐著書'", L"坐著书", L"坐著作'", L"坐著作", L"坐著名'", L"坐著名", L"坐著錄'", L"坐著录", L"坐著稱'", L"坐著称", L"坐著者'", L"坐著者", L"坐著述'", L"坐著述", L"坤乾'", L"坤乾", L"備著'", L"备着", L"備著書'", L"备著书", L"備著作'", L"备著作", L"備著名'", L"备著名", L"備著錄'", L"备著录", L"備著稱'", L"备著称", L"備著者'", L"备著者", L"備著述'", L"备著述", L"天道为乾'", L"天道为乾", L"天道為乾'", L"天道为乾", L"夾著'", L"夹着", L"夾著書'", L"夹著书", L"夾著作'", L"夹著作", L"夾著名'", L"夹著名", L"夾著錄'", L"夹著录", L"夾著稱'", L"夹著称", L"夾著者'", L"夹著者", L"夾著述'", L"夹著述", L"奧區'", L"奧区", L"姓幺'", L"姓幺", L"存摺'", L"存摺", L"孤著'", L"孤着", L"孤著書'", L"孤著书", L"孤著作'", L"孤著作", L"孤著名'", L"孤著名", L"孤著錄'", L"孤著录", L"孤著稱'", L"孤著称", L"孤著者'", L"孤著者", L"孤著述'", L"孤著述", L"學著'", L"学着", L"學著書'", L"学著书", L"學著作'", L"学著作", L"學著名'", L"学著名", L"學著錄'", L"学著录", L"學著稱'", L"学著称", L"學著者'", L"学著者", L"學著述'", L"学著述", L"守著'", L"守着", L"守著書'", L"守著书", L"守著作'", L"守著作", L"守著名'", L"守著名", L"守著錄'", L"守著录", L"守著稱'", L"守著称", L"守著者'", L"守著者", L"守著述'", L"守著述", L"定著'", L"定着", L"定著書'", L"定著书", L"定著作'", L"定著作", L"定著名'", L"定著名", L"定著錄'", L"定著录", L"定著稱'", L"定著称", L"定著者'", L"定著者", L"定著述'", L"定著述", L"對著'", L"对着", L"對著書'", L"对著书", L"對著作'", L"对著作", L"對著名'", L"对著名", L"對著錄'", L"对著录", L"對著稱'", L"对著称", L"對著者'", L"对著者", L"對著述'", L"对著述", L"尋著'", L"寻着", L"尋著書'", L"寻著书", L"尋著作'", L"寻著作", L"尋著名'", L"寻著名", L"尋著錄'", L"寻著录", L"尋著稱'", L"寻著称", L"尋著者'", L"寻著者", L"尋著述'", L"寻著述", L"將軍抽車'", L"将军抽車", L"尼乾陀'", L"尼乾陀", L"展著'", L"展着", L"展著書'", L"展著书", L"展著作'", L"展著作", L"展著名'", L"展著名", L"展著錄'", L"展著录", L"展著稱'", L"展著称", L"展著者'", L"展著者", L"展著述'", L"展著述", L"巨著'", L"巨著", L"帶著'", L"带着", L"帶著書'", L"带著书", L"帶著作'", L"带著作", L"帶著名'", L"带著名", L"帶著錄'", L"带著录", L"帶著稱'", L"带著称", L"帶著者'", L"带著者", L"帶著述'", L"带著述", L"幫著'", L"帮着", L"幫著書'", L"帮著书", L"幫著作'", L"帮著作", L"幫著名'", L"帮著名", L"幫著錄'", L"帮著录", L"幫著稱'", L"帮著称", L"幫著者'", L"帮著者", L"幫著述'", L"帮著述", L"乾乾淨淨'", L"干干净净", L"乾乾脆脆'", L"干干脆脆", L"乾泉水'", L"干泉水", L"幹著'", L"干着", L"么二三'", L"幺二三", L"幺二三'", L"幺二三", L"么元'", L"幺元", L"幺元'", L"幺元", L"幺鳳'", L"幺凤", L"么鳳'", L"幺凤", L"么半群'", L"幺半群", L"幺半群'", L"幺半群", L"幺廝'", L"幺厮", L"幺厮'", L"幺厮", L"么叔'", L"幺叔", L"幺叔'", L"幺叔", L"么媽'", L"幺妈", L"幺媽'", L"幺妈", L"么妹'", L"幺妹", L"幺妹'", L"幺妹", L"么姓'", L"幺姓", L"幺姓'", L"幺姓", L"么姨'", L"幺姨", L"幺姨'", L"幺姨", L"么娘'", L"幺娘", L"么孃'", L"幺娘", L"幺娘'", L"幺娘", L"幺孃'", L"幺娘", L"幺小'", L"幺小", L"么小'", L"幺小", L"幺氏'", L"幺氏", L"么氏'", L"幺氏", L"么爸'", L"幺爸", L"幺爸'", L"幺爸", L"幺爹'", L"幺爹", L"么爹'", L"幺爹", L"么篇'", L"幺篇", L"幺篇'", L"幺篇", L"么舅'", L"幺舅", L"幺舅'", L"幺舅", L"么蛾子'", L"幺蛾子", L"幺蛾子'", L"幺蛾子", L"么謙'", L"幺谦", L"幺謙'", L"幺谦", L"幺麽'", L"幺麽", L"么麼'", L"幺麽", L"幺麽小丑'", L"幺麽小丑", L"么麼小丑'", L"幺麽小丑", L"庇護著'", L"庇护着", L"應著'", L"应着", L"應著書'", L"应著书", L"應著作'", L"应著作", L"應著名'", L"应著名", L"應著錄'", L"应著录", L"應著稱'", L"应著称", L"應著者'", L"应著者", L"應著述'", L"应著述", L"康乾'", L"康乾", L"康著'", L"康着", L"康著書'", L"康著书", L"康著作'", L"康著作", L"康著名'", L"康著名", L"康著錄'", L"康著录", L"康著稱'", L"康著称", L"康著者'", L"康著者", L"康著述'", L"康著述", L"開著'", L"开着", L"開著書'", L"开著书", L"開著作'", L"开著作", L"開著名'", L"开著名", L"開著錄'", L"开著录", L"開著稱'", L"开著称", L"開著者'", L"开著者", L"開著述'", L"开著述", L"張法乾'", L"张法乾", L"當著'", L"当着", L"當著書'", L"当著书", L"當著作'", L"当著作", L"當著名'", L"当著名", L"當著錄'", L"当著录", L"當著稱'", L"当著称", L"當著者'", L"当著者", L"當著述'", L"当著述", L"彰明較著'", L"彰明较著", L"待著'", L"待着", L"待著書'", L"待著书", L"待著作'", L"待著作", L"待著名'", L"待著名", L"待著錄'", L"待著录", L"待著稱'", L"待著称", L"待著者'", L"待著者", L"待著述'", L"待著述", L"得著'", L"得着", L"得著書'", L"得著书", L"得著作'", L"得著作", L"得著名'", L"得著名", L"得著錄'", L"得著录", L"得著稱'", L"得著称", L"得著者'", L"得著者", L"得著述'", L"得著述", L"循著'", L"循着", L"循著書'", L"循著书", L"循著作'", L"循著作", L"循著名'", L"循著名", L"循著錄'", L"循著录", L"循著稱'", L"循著称", L"循著者'", L"循著者", L"循著述'", L"循著述", L"心著'", L"心着", L"心著書'", L"心著书", L"心著作'", L"心著作", L"心著名'", L"心著名", L"心著錄'", L"心著录", L"心著稱'", L"心著称", L"心著者'", L"心著者", L"心著述'", L"心著述", L"忍著'", L"忍着", L"忍著書'", L"忍著书", L"忍著作'", L"忍著作", L"忍著名'", L"忍著名", L"忍著錄'", L"忍著录", L"忍著稱'", L"忍著称", L"忍著者'", L"忍著者", L"忍著述'", L"忍著述", L"志著'", L"志着", L"志著書'", L"志著书", L"志著作'", L"志著作", L"志著名'", L"志著名", L"志著錄'", L"志著录", L"志著稱'", L"志著称", L"志著者'", L"志著者", L"志著述'", L"志著述", L"忙著'", L"忙着", L"忙著書'", L"忙著书", L"忙著作'", L"忙著作", L"忙著名'", L"忙著名", L"忙著錄'", L"忙著录", L"忙著稱'", L"忙著称", L"忙著者'", L"忙著者", L"忙著述'", L"忙著述", L"懷著'", L"怀着", L"懷著書'", L"怀著书", L"懷著作'", L"怀著作", L"懷著名'", L"怀著名", L"懷著錄'", L"怀著录", L"懷著稱'", L"怀著称", L"懷著者'", L"怀著者", L"懷著述'", L"怀著述", L"急著'", L"急着", L"急著書'", L"急著书", L"急著作'", L"急著作", L"急著名'", L"急著名", L"急著錄'", L"急著录", L"急著稱'", L"急著称", L"急著者'", L"急著者", L"急著述'", L"急著述", L"性著'", L"性着", L"性著書'", L"性著书", L"性著作'", L"性著作", L"性著名'", L"性著名", L"性著錄'", L"性著录", L"性著稱'", L"性著称", L"性著者'", L"性著者", L"性著述'", L"性著述", L"戀著'", L"恋着", L"戀著書'", L"恋著书", L"戀著作'", L"恋著作", L"戀著名'", L"恋著名", L"戀著錄'", L"恋著录", L"戀著稱'", L"恋著称", L"戀著者'", L"恋著者", L"戀著述'", L"恋著述", L"恩威並著'", L"恩威并著", L"悠著'", L"悠着", L"悠著書'", L"悠著书", L"悠著作'", L"悠著作", L"悠著名'", L"悠著名", L"悠著錄'", L"悠著录", L"悠著稱'", L"悠著称", L"悠著者'", L"悠著者", L"悠著述'", L"悠著述", L"慣著'", L"惯着", L"慣著書'", L"惯著书", L"慣著作'", L"惯著作", L"慣著名'", L"惯著名", L"慣著錄'", L"惯著录", L"慣著稱'", L"惯著称", L"慣著者'", L"惯著者", L"慣著述'", L"惯著述", L"想著'", L"想着", L"想著書'", L"想著书", L"想著作'", L"想著作", L"想著名'", L"想著名", L"想著錄'", L"想著录", L"想著稱'", L"想著称", L"想著者'", L"想著者", L"想著述'", L"想著述", L"戰著'", L"战着", L"戰著書'", L"战著书", L"戰著作'", L"战著作", L"戰著名'", L"战著名", L"戰著錄'", L"战著录", L"戰著稱'", L"战著称", L"戰著者'", L"战著者", L"戰著述'", L"战著述", L"戴著'", L"戴着", L"戴著書'", L"戴著书", L"戴著作'", L"戴著作", L"戴著名'", L"戴著名", L"戴著錄'", L"戴著录", L"戴著稱'", L"戴著称", L"戴著者'", L"戴著者", L"戴著述'", L"戴著述", L"扎著'", L"扎着", L"扎著書'", L"扎著书", L"扎著作'", L"扎著作", L"扎著名'", L"扎著名", L"扎著錄'", L"扎著录", L"扎著稱'", L"扎著称", L"扎著者'", L"扎著者", L"扎著述'", L"扎著述", L"打著'", L"打着", L"打著書'", L"打著书", L"打著作'", L"打著作", L"打著名'", L"打著名", L"打著錄'", L"打著录", L"打著稱'", L"打著称", L"打著者'", L"打著者", L"打著述'", L"打著述", L"扛著'", L"扛着", L"扛著書'", L"扛著书", L"扛著作'", L"扛著作", L"扛著名'", L"扛著名", L"扛著錄'", L"扛著录", L"扛著稱'", L"扛著称", L"扛著者'", L"扛著者", L"扛著述'", L"扛著述", L"執著'", L"执著", L"找不著'", L"找不着", L"找不著書'", L"找不著书", L"找不著作'", L"找不著作", L"找不著名'", L"找不著名", L"找不著錄'", L"找不著录", L"找不著稱'", L"找不著称", L"找不著者'", L"找不著者", L"找不著述'", L"找不著述", L"抓著'", L"抓着", L"抓著書'", L"抓著书", L"抓著作'", L"抓著作", L"抓著名'", L"抓著名", L"抓著錄'", L"抓著录", L"抓著稱'", L"抓著称", L"抓著者'", L"抓著者", L"抓著述'", L"抓著述", L"護著'", L"护着", L"護著書'", L"护著书", L"護著作'", L"护著作", L"護著名'", L"护著名", L"護著錄'", L"护著录", L"護著稱'", L"护著称", L"護著者'", L"护著者", L"護著述'", L"护著述", L"披著'", L"披着", L"披著書'", L"披著书", L"披著作'", L"披著作", L"披著名'", L"披著名", L"披著錄'", L"披著录", L"披著稱'", L"披著称", L"披著者'", L"披著者", L"披著述'", L"披著述", L"抬著'", L"抬着", L"抬著書'", L"抬著书", L"抬著作'", L"抬著作", L"抬著名'", L"抬著名", L"抬著錄'", L"抬著录", L"抬著稱'", L"抬著称", L"抬著者'", L"抬著者", L"抬著述'", L"抬著述", L"抱著'", L"抱着", L"抱著書'", L"抱著书", L"抱著作'", L"抱著作", L"抱著名'", L"抱著名", L"抱著錄'", L"抱著录", L"抱著稱'", L"抱著称", L"抱著者'", L"抱著者", L"抱著述'", L"抱著述", L"拉著'", L"拉着", L"拉著書'", L"拉著书", L"拉著作'", L"拉著作", L"拉著名'", L"拉著名", L"拉著錄'", L"拉著录", L"拉著稱'", L"拉著称", L"拉著者'", L"拉著者", L"拉著述'", L"拉著述", L"拉鍊'", L"拉链", L"拎著'", L"拎着", L"拎著書'", L"拎著书", L"拎著作'", L"拎著作", L"拎著名'", L"拎著名", L"拎著錄'", L"拎著录", L"拎著稱'", L"拎著称", L"拎著者'", L"拎著者", L"拎著述'", L"拎著述", L"拖著'", L"拖着", L"拖著書'", L"拖著书", L"拖著作'", L"拖著作", L"拖著名'", L"拖著名", L"拖著錄'", L"拖著录", L"拖著稱'", L"拖著称", L"拖著者'", L"拖著者", L"拖著述'", L"拖著述", L"拙著'", L"拙著", L"拚命'", L"拚命", L"拚搏'", L"拚搏", L"拚死'", L"拚死", L"拼著'", L"拼着", L"拼著書'", L"拼著书", L"拼著作'", L"拼著作", L"拼著名'", L"拼著名", L"拼著錄'", L"拼著录", L"拼著稱'", L"拼著称", L"拼著者'", L"拼著者", L"拼著述'", L"拼著述", L"拿著'", L"拿着", L"拿著書'", L"拿著书", L"拿著作'", L"拿著作", L"拿著名'", L"拿著名", L"拿著錄'", L"拿著录", L"拿著稱'", L"拿著称", L"拿著者'", L"拿著者", L"拿著述'", L"拿著述", L"持著'", L"持着", L"持著書'", L"持著书", L"持著作'", L"持著作", L"持著名'", L"持著名", L"持著錄'", L"持著录", L"持著稱'", L"持著称", L"持著者'", L"持著者", L"持著述'", L"持著述", L"挑著'", L"挑着", L"挑著書'", L"挑著书", L"挑著作'", L"挑著作", L"挑著名'", L"挑著名", L"挑著錄'", L"挑著录", L"挑著稱'", L"挑著称", L"挑著者'", L"挑著者", L"挑著述'", L"挑著述", L"擋著'", L"挡着", L"擋著書'", L"挡著书", L"擋著作'", L"挡著作", L"擋著名'", L"挡著名", L"擋著錄'", L"挡著录", L"擋著稱'", L"挡著称", L"擋著者'", L"挡著者", L"擋著述'", L"挡著述", L"掙著'", L"挣着", L"掙著書'", L"挣著书", L"掙著作'", L"挣著作", L"掙著名'", L"挣著名", L"掙著錄'", L"挣著录", L"掙著稱'", L"挣著称", L"掙著者'", L"挣著者", L"掙著述'", L"挣著述", L"揮著'", L"挥着", L"揮著書'", L"挥著书", L"揮著作'", L"挥著作", L"揮著名'", L"挥著名", L"揮著錄'", L"挥著录", L"揮著稱'", L"挥著称", L"揮著者'", L"挥著者", L"揮著述'", L"挥著述", L"挨著'", L"挨着", L"挨著書'", L"挨著书", L"挨著作'", L"挨著作", L"挨著名'", L"挨著名", L"挨著錄'", L"挨著录", L"挨著稱'", L"挨著称", L"挨著者'", L"挨著者", L"挨著述'", L"挨著述", L"捆著'", L"捆着", L"捆著書'", L"捆著书", L"捆著作'", L"捆著作", L"捆著名'", L"捆著名", L"捆著錄'", L"捆著录", L"捆著稱'", L"捆著称", L"捆著者'", L"捆著者", L"捆著述'", L"捆著述", L"據著'", L"据着", L"據著書'", L"据著书", L"據著作'", L"据著作", L"據著名'", L"据著名", L"據著錄'", L"据著录", L"據著稱'", L"据著称", L"據著者'", L"据著者", L"據著述'", L"据著述", L"掖著'", L"掖着", L"掖著書'", L"掖著书", L"掖著作'", L"掖著作", L"掖著名'", L"掖著名", L"掖著錄'", L"掖著录", L"掖著稱'", L"掖著称", L"掖著者'", L"掖著者", L"掖著述'", L"掖著述", L"接著'", L"接着", L"接著書'", L"接著书", L"接著作'", L"接著作", L"接著名'", L"接著名", L"接著錄'", L"接著录", L"接著稱'", L"接著称", L"接著者'", L"接著者", L"接著述'", L"接著述", L"揉著'", L"揉着", L"揉著書'", L"揉著书", L"揉著作'", L"揉著作", L"揉著名'", L"揉著名", L"揉著錄'", L"揉著录", L"揉著稱'", L"揉著称", L"揉著者'", L"揉著者", L"揉著述'", L"揉著述", L"提著'", L"提着", L"提著書'", L"提著书", L"提著作'", L"提著作", L"提著名'", L"提著名", L"提著錄'", L"提著录", L"提著稱'", L"提著称", L"提著者'", L"提著者", L"提著述'", L"提著述", L"摟著'", L"搂着", L"摟著書'", L"搂著书", L"摟著作'", L"搂著作", L"摟著名'", L"搂著名", L"摟著錄'", L"搂著录", L"摟著稱'", L"搂著称", L"摟著者'", L"搂著者", L"摟著述'", L"搂著述", L"擺著'", L"摆着", L"擺著書'", L"摆著书", L"擺著作'", L"摆著作", L"擺著名'", L"摆著名", L"擺著錄'", L"摆著录", L"擺著稱'", L"摆著称", L"擺著者'", L"摆著者", L"擺著述'", L"摆著述", L"撰著'", L"撰著", L"撼著'", L"撼着", L"撼著書'", L"撼著书", L"撼著作'", L"撼著作", L"撼著名'", L"撼著名", L"撼著錄'", L"撼著录", L"撼著稱'", L"撼著称", L"撼著者'", L"撼著者", L"撼著述'", L"撼著述", L"敞著'", L"敞着", L"敞著書'", L"敞著书", L"敞著作'", L"敞著作", L"敞著名'", L"敞著名", L"敞著錄'", L"敞著录", L"敞著稱'", L"敞著称", L"敞著者'", L"敞著者", L"敞著述'", L"敞著述", L"數著'", L"数着", L"數著書'", L"数著书", L"數著作'", L"数著作", L"數著名'", L"数著名", L"數著錄'", L"数著录", L"數著稱'", L"数著称", L"數著者'", L"数著者", L"數著述'", L"数著述", L"斗著'", L"斗着", L"斗著書'", L"斗著书", L"斗著作'", L"斗著作", L"斗著名'", L"斗著名", L"斗著錄'", L"斗著录", L"斗著稱'", L"斗著称", L"斗著者'", L"斗著者", L"斗著述'", L"斗著述", L"斥著'", L"斥着", L"斥著書'", L"斥著书", L"斥著作'", L"斥著作", L"斥著名'", L"斥著名", L"斥著錄'", L"斥著录", L"斥著稱'", L"斥著称", L"斥著者'", L"斥著者", L"斥著述'", L"斥著述", L"新著'", L"新著", L"新著龍虎門'", L"新著龙虎门", L"於世成'", L"於世成", L"於乎'", L"於乎", L"於乙于同'", L"於乙于同", L"於乙宇同'", L"於乙宇同", L"於于同'", L"於于同", L"於哲'", L"於哲", L"於夫罗'", L"於夫罗", L"於夫羅'", L"於夫罗", L"於姓'", L"於姓", L"於宇同'", L"於宇同", L"於崇文'", L"於崇文", L"於志賀'", L"於志贺", L"於志贺'", L"於志贺", L"於戲'", L"於戏", L"於梨華'", L"於梨华", L"於梨华'", L"於梨华", L"於氏'", L"於氏", L"於潛縣'", L"於潜县", L"於潜县'", L"於潜县", L"於祥玉'", L"於祥玉", L"於菟'", L"於菟", L"於賢德'", L"於贤德", L"於除鞬'", L"於除鞬", L"旋乾轉坤'", L"旋乾转坤", L"曠若發矇'", L"旷若发矇", L"昂著'", L"昂着", L"昂著書'", L"昂著书", L"昂著作'", L"昂著作", L"昂著名'", L"昂著名", L"昂著錄'", L"昂著录", L"昂著稱'", L"昂著称", L"昂著者'", L"昂著者", L"昂著述'", L"昂著述", L"易·乾'", L"易·乾", L"易經·乾'", L"易经·乾", L"易经·乾'", L"易经·乾", L"易經乾'", L"易经乾", L"易经乾'", L"易经乾", L"映著'", L"映着", L"映著書'", L"映著书", L"映著作'", L"映著作", L"映著名'", L"映著名", L"映著錄'", L"映著录", L"映著稱'", L"映著称", L"映著者'", L"映著者", L"映著述'", L"映著述", L"昭著'", L"昭著", L"顯著'", L"显著", L"显著'", L"显著", L"晃著'", L"晃着", L"晃著書'", L"晃著书", L"晃著作'", L"晃著作", L"晃著名'", L"晃著名", L"晃著錄'", L"晃著录", L"晃著稱'", L"晃著称", L"晃著者'", L"晃著者", L"晃著述'", L"晃著述", L"暗著'", L"暗着", L"暗著書'", L"暗著书", L"暗著作'", L"暗著作", L"暗著名'", L"暗著名", L"暗著錄'", L"暗著录", L"暗著稱'", L"暗著称", L"暗著者'", L"暗著者", L"暗著述'", L"暗著述", L"有著'", L"有着", L"有著書'", L"有著书", L"有著作'", L"有著作", L"有著名'", L"有著名", L"有著錄'", L"有著录", L"有著稱'", L"有著称", L"有著者'", L"有著者", L"有著述'", L"有著述", L"望著'", L"望着", L"望著書'", L"望著书", L"望著作'", L"望著作", L"望著名'", L"望著名", L"望著錄'", L"望著录", L"望著稱'", L"望著称", L"望著者'", L"望著者", L"望著述'", L"望著述", L"朝乾夕惕'", L"朝乾夕惕", L"朝著'", L"朝着", L"朝著書'", L"朝著书", L"朝著作'", L"朝著作", L"朝著名'", L"朝著名", L"朝著錄'", L"朝著录", L"朝著稱'", L"朝著称", L"朝著者'", L"朝著者", L"朝著述'", L"朝著述", L"本著'", L"本着", L"本著書'", L"本著书", L"本著作'", L"本著作", L"本著名'", L"本著名", L"本著錄'", L"本著录", L"本著稱'", L"本著称", L"本著者'", L"本著者", L"本著述'", L"本著述", L"朴於宇同'", L"朴於宇同", L"殺著'", L"杀着", L"殺著書'", L"杀著书", L"殺著作'", L"杀著作", L"殺著名'", L"杀著名", L"殺著錄'", L"杀著录", L"殺著稱'", L"杀著称", L"殺著者'", L"杀著者", L"殺著述'", L"杀著述", L"雜著'", L"杂着", L"雜著書'", L"杂著书", L"雜著作'", L"杂著作", L"雜著名'", L"杂著名", L"雜著錄'", L"杂著录", L"雜著稱'", L"杂著称", L"雜著者'", L"杂著者", L"雜著述'", L"杂著述", L"李乾德'", L"李乾德", L"李乾順'", L"李乾顺", L"李乾顺'", L"李乾顺", L"李澤鉅'", L"李泽钜", L"來著'", L"来着", L"來著書'", L"来著书", L"來著作'", L"来著作", L"來著名'", L"来著名", L"來著錄'", L"来著录", L"來著稱'", L"来著称", L"來著者'", L"来著者", L"來著述'", L"来著述", L"楊幺'", L"杨幺", L"枕著'", L"枕着", L"枕著書'", L"枕著书", L"枕著作'", L"枕著作", L"枕著名'", L"枕著名", L"枕著錄'", L"枕著录", L"枕著稱'", L"枕著称", L"枕著者'", L"枕著者", L"枕著述'", L"枕著述", L"柳詒徵'", L"柳诒徵", L"柳诒徵'", L"柳诒徵", L"標志著'", L"标志着", L"標誌著'", L"标志着", L"夢著'", L"梦着", L"夢著書'", L"梦著书", L"夢著作'", L"梦著作", L"夢著名'", L"梦著名", L"夢著錄'", L"梦著录", L"夢著稱'", L"梦著称", L"夢著者'", L"梦著者", L"夢著述'", L"梦著述", L"梳著'", L"梳着", L"梳著書'", L"梳著书", L"梳著作'", L"梳著作", L"梳著名'", L"梳著名", L"梳著錄'", L"梳著录", L"梳著稱'", L"梳著称", L"梳著者'", L"梳著者", L"梳著述'", L"梳著述", L"樊於期'", L"樊於期", L"氆氌'", L"氆氌", L"求著'", L"求着", L"求著書'", L"求著书", L"求著作'", L"求著作", L"求著名'", L"求著名", L"求著錄'", L"求著录", L"求著稱'", L"求著称", L"求著者'", L"求著者", L"求著述'", L"求著述", L"沈沒'", L"沉没", L"沉著'", L"沉着", L"沈積'", L"沉积", L"沈船'", L"沉船", L"沉著書'", L"沉著书", L"沉著作'", L"沉著作", L"沉著名'", L"沉著名", L"沉著錄'", L"沉著录", L"沉著稱'", L"沉著称", L"沉著者'", L"沉著者", L"沉著述'", L"沉著述", L"沈默'", L"沉默", L"沿著'", L"沿着", L"沿著書'", L"沿著书", L"沿著作'", L"沿著作", L"沿著名'", L"沿著名", L"沿著錄'", L"沿著录", L"沿著稱'", L"沿著称", L"沿著者'", L"沿著者", L"沿著述'", L"沿著述", L"氾濫'", L"泛滥", L"洗鍊'", L"洗练", L"活著'", L"活着", L"活著書'", L"活著书", L"活著作'", L"活著作", L"活著名'", L"活著名", L"活著錄'", L"活著录", L"活著稱'", L"活著称", L"活著者'", L"活著者", L"活著述'", L"活著述", L"流著'", L"流着", L"流著書'", L"流著书", L"流著作'", L"流著作", L"流著名'", L"流著名", L"流著錄'", L"流著录", L"流著稱'", L"流著称", L"流著者'", L"流著者", L"流著述'", L"流著述", L"流露著'", L"流露着", L"浮著'", L"浮着", L"浮著書'", L"浮著书", L"浮著作'", L"浮著作", L"浮著名'", L"浮著名", L"浮著錄'", L"浮著录", L"浮著稱'", L"浮著称", L"浮著者'", L"浮著者", L"浮著述'", L"浮著述", L"潤著'", L"润着", L"潤著書'", L"润著书", L"潤著作'", L"润著作", L"潤著名'", L"润著名", L"潤著錄'", L"润著录", L"潤著稱'", L"润著称", L"潤著者'", L"润著者", L"潤著述'", L"润著述", L"涵著'", L"涵着", L"涵著書'", L"涵著书", L"涵著作'", L"涵著作", L"涵著名'", L"涵著名", L"涵著錄'", L"涵著录", L"涵著稱'", L"涵著称", L"涵著者'", L"涵著者", L"涵著述'", L"涵著述", L"渴著'", L"渴着", L"渴著書'", L"渴著书", L"渴著作'", L"渴著作", L"渴著名'", L"渴著名", L"渴著錄'", L"渴著录", L"渴著稱'", L"渴著称", L"渴著者'", L"渴著者", L"渴著述'", L"渴著述", L"溢著'", L"溢着", L"溢著書'", L"溢著书", L"溢著作'", L"溢著作", L"溢著名'", L"溢著名", L"溢著錄'", L"溢著录", L"溢著稱'", L"溢著称", L"溢著者'", L"溢著者", L"溢著述'", L"溢著述", L"演著'", L"演着", L"演著書'", L"演著书", L"演著作'", L"演著作", L"演著名'", L"演著名", L"演著錄'", L"演著录", L"演著稱'", L"演著称", L"演著者'", L"演著者", L"演著述'", L"演著述", L"漫著'", L"漫着", L"漫著書'", L"漫著书", L"漫著作'", L"漫著作", L"漫著名'", L"漫著名", L"漫著錄'", L"漫著录", L"漫著稱'", L"漫著称", L"漫著者'", L"漫著者", L"漫著述'", L"漫著述", L"點著'", L"点着", L"點著作'", L"点著作", L"點著名'", L"点著名", L"點著錄'", L"点著录", L"點著稱'", L"点著称", L"點著者'", L"点著者", L"點著述'", L"点著述", L"燒著'", L"烧着", L"燒著作'", L"烧著作", L"燒著名'", L"烧著名", L"燒著錄'", L"烧著录", L"燒著稱'", L"烧著称", L"燒著者'", L"烧著者", L"燒著述'", L"烧著述", L"照著'", L"照着", L"照著書'", L"照著书", L"照著作'", L"照著作", L"照著名'", L"照著名", L"照著錄'", L"照著录", L"照著稱'", L"照著称", L"照著者'", L"照著者", L"照著述'", L"照著述", L"愛護著'", L"爱护着", L"愛著'", L"爱着", L"愛著書'", L"爱著书", L"愛著作'", L"爱著作", L"愛著名'", L"爱著名", L"愛著錄'", L"爱著录", L"愛著稱'", L"爱著称", L"愛著者'", L"爱著者", L"愛著述'", L"爱著述", L"牽著'", L"牵着", L"牽著書'", L"牵著书", L"牽著作'", L"牵著作", L"牽著名'", L"牵著名", L"牽著錄'", L"牵著录", L"牽著稱'", L"牵著称", L"牽著者'", L"牵著者", L"牽著述'", L"牵著述", L"犯不著'", L"犯不着", L"獨著'", L"独着", L"獨著書'", L"独著书", L"獨著作'", L"独著作", L"獨著名'", L"独著名", L"獨著錄'", L"独著录", L"獨著稱'", L"独著称", L"獨著者'", L"独著者", L"獨著述'", L"独著述", L"猜著'", L"猜着", L"猜著書'", L"猜着书", L"猜著作'", L"猜著作", L"猜著名'", L"猜著名", L"猜著錄'", L"猜著录", L"猜著稱'", L"猜著称", L"猜著者'", L"猜著者", L"猜著述'", L"猜著述", L"玩著'", L"玩着", L"甜著'", L"甜着", L"甜著書'", L"甜著书", L"甜著作'", L"甜著作", L"甜著名'", L"甜著名", L"甜著錄'", L"甜著录", L"甜著稱'", L"甜著称", L"甜著者'", L"甜著者", L"甜著述'", L"甜著述", L"用不著'", L"用不着", L"用不著書'", L"用不着书", L"用不著作'", L"用不著作", L"用不著名'", L"用不著名", L"用不著錄'", L"用不著录", L"用不著稱'", L"用不著称", L"用不著者'", L"用不著者", L"用不著述'", L"用不著述", L"用著'", L"用着", L"用著書'", L"用著书", L"用著作'", L"用著作", L"用著名'", L"用著名", L"用著錄'", L"用著录", L"用著稱'", L"用著称", L"用著者'", L"用著者", L"用著述'", L"用著述", L"留著'", L"留着", L"留著書'", L"留着书", L"留著作'", L"留著作", L"留著名'", L"留著名", L"留著錄'", L"留著录", L"留著稱'", L"留著称", L"留著者'", L"留著者", L"留著述'", L"留著述", L"疑著'", L"疑着", L"疑著書'", L"疑著书", L"疑著作'", L"疑著作", L"疑著名'", L"疑著名", L"疑著錄'", L"疑著录", L"疑著稱'", L"疑著称", L"疑著者'", L"疑著者", L"疑著述'", L"疑著述", L"癥瘕'", L"癥瘕", L"皺著'", L"皱着", L"皺著書'", L"皱著书", L"皺著作'", L"皱著作", L"皺著名'", L"皱著名", L"皺著錄'", L"皱著录", L"皺著稱'", L"皱著称", L"皺著者'", L"皱著者", L"皺著述'", L"皱著述", L"盛著'", L"盛着", L"盛著書'", L"盛著书", L"盛著作'", L"盛著作", L"盛著名'", L"盛著名", L"盛著錄'", L"盛著录", L"盛著稱'", L"盛著称", L"盛著者'", L"盛著者", L"盛著述'", L"盛著述", L"盯著'", L"盯着", L"盯著書'", L"盯着书", L"盯著作'", L"盯著作", L"盯著名'", L"盯著名", L"盯著錄'", L"盯著录", L"盯著稱'", L"盯著称", L"盯著者'", L"盯著者", L"盯著述'", L"盯著述", L"盾著'", L"盾着", L"盾著書'", L"盾著书", L"盾著作'", L"盾著作", L"盾著名'", L"盾著名", L"盾著錄'", L"盾著录", L"盾著稱'", L"盾著称", L"盾著者'", L"盾著者", L"盾著述'", L"盾著述", L"看著'", L"看着", L"看著書'", L"看着书", L"看著作'", L"看著作", L"看著名'", L"看著名", L"看著錄'", L"看著录", L"看著稱'", L"看著称", L"看著者'", L"看著者", L"看著述'", L"看著述", L"著業'", L"着业", L"著絲'", L"着丝", L"著么'", L"着么", L"著人'", L"着人", L"著什么急'", L"着什么急", L"著他'", L"着他", L"著令'", L"着令", L"著位'", L"着位", L"著體'", L"着体", L"著你'", L"着你", L"著便'", L"着便", L"著涼'", L"着凉", L"著力'", L"着力", L"著勁'", L"着劲", L"著號'", L"着号", L"著呢'", L"着呢", L"著哩'", L"着哩", L"著地'", L"着地", L"著墨'", L"着墨", L"著聲'", L"着声", L"著處'", L"着处", L"著她'", L"着她", L"著妳'", L"着妳", L"著姓'", L"着姓", L"著它'", L"着它", L"著定'", L"着定", L"著實'", L"着实", L"著己'", L"着己", L"著帳'", L"着帐", L"著床'", L"着床", L"著庸'", L"着庸", L"著式'", L"着式", L"著錄'", L"着录", L"著心'", L"着心", L"著志'", L"着志", L"著忙'", L"着忙", L"著急'", L"着急", L"著惱'", L"着恼", L"著驚'", L"着惊", L"著想'", L"着想", L"著意'", L"着意", L"著慌'", L"着慌", L"著我'", L"着我", L"著手'", L"着手", L"著抹'", L"着抹", L"著摸'", L"着摸", L"著撰'", L"着撰", L"著數'", L"着数", L"著明'", L"着明", L"著末'", L"着末", L"著極'", L"着极", L"著格'", L"着格", L"著棋'", L"着棋", L"著槁'", L"着槁", L"著氣'", L"着气", L"著法'", L"着法", L"著淺'", L"着浅", L"著火'", L"着火", L"著然'", L"着然", L"著甚'", L"着甚", L"著生'", L"着生", L"著疑'", L"着疑", L"著白'", L"着白", L"著相'", L"着相", L"著眼'", L"着眼", L"著著'", L"着着", L"著祂'", L"着祂", L"著積'", L"着积", L"著稿'", L"着稿", L"著筆'", L"着笔", L"著籍'", L"着籍", L"著緊'", L"着紧", L"著緑'", L"着緑", L"著絆'", L"着绊", L"著績'", L"着绩", L"著緋'", L"着绯", L"著綠'", L"着绿", L"著肉'", L"着肉", L"著腳'", L"着脚", L"著艦'", L"着舰", L"著色'", L"着色", L"著節'", L"着节", L"著花'", L"着花", L"著莫'", L"着莫", L"著落'", L"着落", L"著藁'", L"着藁", L"著衣'", L"着衣", L"著裝'", L"着装", L"著要'", L"着要", L"著警'", L"着警", L"著趣'", L"着趣", L"著邊'", L"着边", L"著迷'", L"着迷", L"著跡'", L"着迹", L"著重'", L"着重", L"著録'", L"着録", L"著聞'", L"着闻", L"著陸'", L"着陆", L"著雝'", L"着雝", L"著鞭'", L"着鞭", L"著題'", L"着题", L"著魔'", L"着魔", L"睡不著'", L"睡不着", L"睡不著書'", L"睡不著书", L"睡不著作'", L"睡不著作", L"睡不著名'", L"睡不著名", L"睡不著錄'", L"睡不著录", L"睡不著稱'", L"睡不著称", L"睡不著者'", L"睡不著者", L"睡不著述'", L"睡不著述", L"睡著'", L"睡着", L"睡著書'", L"睡著书", L"睡著作'", L"睡著作", L"睡著名'", L"睡著名", L"睡著錄'", L"睡著录", L"睡著稱'", L"睡著称", L"睡著者'", L"睡著者", L"睡著述'", L"睡著述", L"睹微知著'", L"睹微知著", L"睪丸'", L"睾丸", L"瞞著'", L"瞒着", L"瞞著書'", L"瞒著书", L"瞞著作'", L"瞒著作", L"瞞著名'", L"瞒著名", L"瞞著錄'", L"瞒著录", L"瞞著稱'", L"瞒著称", L"瞞著者'", L"瞒著者", L"瞞著述'", L"瞒著述", L"瞧著'", L"瞧着", L"瞧著書'", L"瞧着书", L"瞧著作'", L"瞧著作", L"瞧著名'", L"瞧著名", L"瞧著錄'", L"瞧著录", L"瞧著稱'", L"瞧著称", L"瞧著者'", L"瞧著者", L"瞧著述'", L"瞧著述", L"瞪著'", L"瞪着", L"瞪著書'", L"瞪著书", L"瞪著作'", L"瞪著作", L"瞪著名'", L"瞪著名", L"瞪著錄'", L"瞪著录", L"瞪著稱'", L"瞪著称", L"瞪著者'", L"瞪著者", L"瞪著述'", L"瞪著述", L"瞭望'", L"瞭望", L"石碁镇'", L"石碁镇", L"石碁鎮'", L"石碁镇", L"福著'", L"福着", L"福著書'", L"福著书", L"福著作'", L"福著作", L"福著名'", L"福著名", L"福著錄'", L"福著录", L"福著稱'", L"福著称", L"福著者'", L"福著者", L"福著述'", L"福著述", L"穀梁'", L"穀梁", L"空著'", L"空着", L"空著書'", L"空著书", L"空著作'", L"空著作", L"空著名'", L"空著名", L"空著錄'", L"空著录", L"空著稱'", L"空著称", L"空著者'", L"空著者", L"空著述'", L"空著述", L"穿著'", L"穿着", L"穿著書'", L"穿著书", L"穿著作'", L"穿著作", L"穿著名'", L"穿著名", L"穿著錄'", L"穿著录", L"穿著稱'", L"穿著称", L"穿著者'", L"穿著者", L"穿著述'", L"穿著述", L"豎著'", L"竖着", L"豎著書'", L"竖著书", L"豎著作'", L"竖著作", L"豎著名'", L"竖著名", L"豎著錄'", L"竖著录", L"豎著稱'", L"竖著称", L"豎著者'", L"竖著者", L"豎著述'", L"竖著述", L"站著'", L"站着", L"站著書'", L"站著书", L"站著作'", L"站著作", L"站著名'", L"站著名", L"站著錄'", L"站著录", L"站著稱'", L"站著称", L"站著者'", L"站著者", L"站著述'", L"站著述", L"笑著'", L"笑着", L"笑著書'", L"笑著书", L"笑著作'", L"笑著作", L"笑著名'", L"笑著名", L"笑著錄'", L"笑著录", L"笑著稱'", L"笑著称", L"笑著者'", L"笑著者", L"笑著述'", L"笑著述", L"答覆'", L"答复", L"管著'", L"管着", L"管著書'", L"管著书", L"管著作'", L"管著作", L"管著名'", L"管著名", L"管著錄'", L"管著录", L"管著稱'", L"管著称", L"管著者'", L"管著者", L"管著述'", L"管著述", L"綁著'", L"绑着", L"綁著書'", L"绑著书", L"綁著作'", L"绑著作", L"綁著名'", L"绑著名", L"綁著錄'", L"绑著录", L"綁著稱'", L"绑著称", L"綁著者'", L"绑著者", L"綁著述'", L"绑著述", L"繞著'", L"绕着", L"繞著書'", L"绕著书", L"繞著作'", L"绕著作", L"繞著名'", L"绕著名", L"繞著錄'", L"绕著录", L"繞著稱'", L"绕著称", L"繞著者'", L"绕著者", L"繞著述'", L"绕著述", L"編著'", L"编著", L"纏著'", L"缠着", L"纏著書'", L"缠著书", L"纏著作'", L"缠著作", L"纏著名'", L"缠著名", L"纏著錄'", L"缠著录", L"纏著稱'", L"缠著称", L"纏著者'", L"缠著者", L"纏著述'", L"缠著述", L"罩著'", L"罩着", L"罩著書'", L"罩著书", L"罩著作'", L"罩著作", L"罩著名'", L"罩著名", L"罩著錄'", L"罩著录", L"罩著稱'", L"罩著称", L"罩著者'", L"罩著者", L"罩著述'", L"罩著述", L"美著'", L"美着", L"美著書'", L"美著书", L"美著作'", L"美著作", L"美著名'", L"美著名", L"美著錄'", L"美著录", L"美著稱'", L"美著称", L"美著者'", L"美著者", L"美著述'", L"美著述", L"耀著'", L"耀着", L"耀著書'", L"耀著书", L"耀著作'", L"耀著作", L"耀著名'", L"耀著名", L"耀著錄'", L"耀著录", L"耀著稱'", L"耀著称", L"耀著者'", L"耀著者", L"耀著述'", L"耀著述", L"老幺'", L"老幺", L"考著'", L"考着", L"考著書'", L"考著书", L"考著作'", L"考著作", L"考著名'", L"考著名", L"考著錄'", L"考著录", L"考著稱'", L"考著称", L"考著者'", L"考著者", L"考著述'", L"考著述", L"肉乾乾'", L"肉干干", L"肘手鍊足'", L"肘手链足", L"背著'", L"背着", L"背著書'", L"背著书", L"背著作'", L"背著作", L"背著名'", L"背著名", L"背著錄'", L"背著录", L"背著稱'", L"背著称", L"背著者'", L"背著者", L"背著述'", L"背著述", L"膠著'", L"胶着", L"膠著書'", L"胶著书", L"膠著作'", L"胶著作", L"膠著名'", L"胶著名", L"膠著錄'", L"胶著录", L"膠著稱'", L"胶著称", L"膠著者'", L"胶著者", L"膠著述'", L"胶著述", L"藝著'", L"艺着", L"藝著書'", L"艺著书", L"藝著作'", L"艺著作", L"藝著名'", L"艺著名", L"藝著錄'", L"艺著录", L"藝著稱'", L"艺著称", L"藝著者'", L"艺著者", L"藝著述'", L"艺著述", L"苦著'", L"苦着", L"苦著書'", L"苦著书", L"苦著作'", L"苦著作", L"苦著名'", L"苦著名", L"苦著錄'", L"苦著录", L"苦著稱'", L"苦著称", L"苦著者'", L"苦著者", L"苦著述'", L"苦著述", L"苧烯'", L"苧烯", L"薴烯'", L"苧烯", L"獲著'", L"获着", L"獲著書'", L"获著书", L"獲著作'", L"获著作", L"獲著名'", L"获著名", L"獲著錄'", L"获著录", L"獲著稱'", L"获著称", L"獲著者'", L"获著者", L"獲著述'", L"获著述", L"蕭乾'", L"萧乾", L"萧乾'", L"萧乾", L"落著'", L"落着", L"落著書'", L"落著书", L"落著作'", L"落著作", L"落著名'", L"落著名", L"落著錄'", L"落著录", L"落著稱'", L"落著称", L"落著者'", L"落著者", L"落著述'", L"落著述", L"著書'", L"著书", L"著書立說'", L"著书立说", L"著作'", L"著作", L"著名'", L"著名", L"著錄規則'", L"著录规则", L"著文'", L"著文", L"著有'", L"著有", L"著稱'", L"著称", L"著者'", L"著者", L"著身'", L"著身", L"著述'", L"著述", L"蒙著'", L"蒙着", L"蒙著書'", L"蒙著书", L"蒙著作'", L"蒙著作", L"蒙著名'", L"蒙著名", L"蒙著錄'", L"蒙著录", L"蒙著稱'", L"蒙著称", L"蒙著者'", L"蒙著者", L"蒙著述'", L"蒙著述", L"藏著'", L"藏着", L"藏著書'", L"藏著书", L"藏著作'", L"藏著作", L"藏著名'", L"藏著名", L"藏著錄'", L"藏著录", L"藏著稱'", L"藏著称", L"藏著者'", L"藏著者", L"藏著述'", L"藏著述", L"蘸著'", L"蘸着", L"蘸著書'", L"蘸著书", L"蘸著作'", L"蘸著作", L"蘸著名'", L"蘸著名", L"蘸著錄'", L"蘸著录", L"蘸著稱'", L"蘸著称", L"蘸著者'", L"蘸著者", L"蘸著述'", L"蘸著述", L"行著'", L"行着", L"行著書'", L"行著书", L"行著作'", L"行著作", L"行著名'", L"行著名", L"行著錄'", L"行著录", L"行著稱'", L"行著称", L"行著者'", L"行著者", L"行著述'", L"行著述", L"衣著'", L"衣着", L"衣著書'", L"衣著书", L"衣著作'", L"衣著作", L"衣著名'", L"衣著名", L"衣著錄'", L"衣著录", L"衣著稱'", L"衣著称", L"衣著者'", L"衣著者", L"衣著述'", L"衣著述", L"裝著'", L"装着", L"裝著書'", L"装著书", L"裝著作'", L"装著作", L"裝著名'", L"装著名", L"裝著錄'", L"装著录", L"裝著稱'", L"装著称", L"裝著者'", L"装著者", L"裝著述'", L"装著述", L"裹著'", L"裹着", L"裹著書'", L"裹著书", L"裹著作'", L"裹著作", L"裹著名'", L"裹著名", L"裹著錄'", L"裹著录", L"裹著稱'", L"裹著称", L"裹著者'", L"裹著者", L"裹著述'", L"裹著述", L"覆蓋'", L"覆蓋", L"見微知著'", L"见微知著", L"見著'", L"见着", L"見著書'", L"见著书", L"見著作'", L"见著作", L"見著名'", L"见著名", L"見著錄'", L"见著录", L"見著稱'", L"见著称", L"見著者'", L"见著者", L"見著述'", L"见著述", L"視微知著'", L"视微知著", L"言幾析理'", L"言幾析理", L"記著'", L"记着", L"記著書'", L"记著书", L"記著作'", L"记著作", L"記著名'", L"记著名", L"記著錄'", L"记著录", L"記著稱'", L"记著称", L"記著者'", L"记著者", L"記著述'", L"记著述", L"論著'", L"论著", L"譯著'", L"译著", L"試著'", L"试着", L"試著書'", L"试著书", L"試著作'", L"试著作", L"試著名'", L"试著名", L"試著錄'", L"试著录", L"試著稱'", L"试著称", L"試著者'", L"试著者", L"試著述'", L"试著述", L"語著'", L"语着", L"語著書'", L"语著书", L"語著作'", L"语著作", L"語著名'", L"语著名", L"語著錄'", L"语著录", L"語著稱'", L"语著称", L"語著者'", L"语著者", L"語著述'", L"语著述", L"豫著'", L"豫着", L"豫著書'", L"豫著书", L"豫著作'", L"豫著作", L"豫著名'", L"豫著名", L"豫著錄'", L"豫著录", L"豫著稱'", L"豫著称", L"豫著者'", L"豫著者", L"豫著述'", L"豫著述", L"貞著'", L"贞着", L"貞著書'", L"贞著书", L"貞著作'", L"贞著作", L"貞著名'", L"贞著名", L"貞著錄'", L"贞著录", L"貞著稱'", L"贞著称", L"貞著者'", L"贞著者", L"貞著述'", L"贞著述", L"走著'", L"走着", L"走著書'", L"走著书", L"走著作'", L"走著作", L"走著名'", L"走著名", L"走著錄'", L"走著录", L"走著稱'", L"走著称", L"走著者'", L"走著者", L"走著述'", L"走著述", L"趕著'", L"赶着", L"趕著書'", L"赶著书", L"趕著作'", L"赶著作", L"趕著名'", L"赶著名", L"趕著錄'", L"赶著录", L"趕著稱'", L"赶著称", L"趕著者'", L"赶著者", L"趕著述'", L"赶著述", L"趴著'", L"趴着", L"趴著書'", L"趴著书", L"趴著作'", L"趴著作", L"趴著名'", L"趴著名", L"趴著錄'", L"趴著录", L"趴著稱'", L"趴著称", L"趴著者'", L"趴著者", L"趴著述'", L"趴著述", L"躍著'", L"跃着", L"躍著書'", L"跃著书", L"躍著作'", L"跃著作", L"躍著名'", L"跃著名", L"躍著錄'", L"跃著录", L"躍著稱'", L"跃著称", L"躍著者'", L"跃著者", L"躍著述'", L"跃著述", L"跑著'", L"跑着", L"跑著書'", L"跑著书", L"跑著作'", L"跑著作", L"跑著名'", L"跑著名", L"跑著錄'", L"跑著录", L"跑著稱'", L"跑著称", L"跑著者'", L"跑著者", L"跑著述'", L"跑著述", L"跟著'", L"跟着", L"跟著書'", L"跟著书", L"跟著作'", L"跟著作", L"跟著名'", L"跟著名", L"跟著錄'", L"跟著录", L"跟著稱'", L"跟著称", L"跟著者'", L"跟著者", L"跟著述'", L"跟著述", L"跪著'", L"跪着", L"跪著書'", L"跪著书", L"跪著作'", L"跪著作", L"跪著名'", L"跪著名", L"跪著錄'", L"跪著录", L"跪著稱'", L"跪著称", L"跪著者'", L"跪著者", L"跪著述'", L"跪著述", L"跳著'", L"跳着", L"跳著書'", L"跳著书", L"跳著作'", L"跳著作", L"跳著名'", L"跳著名", L"跳著錄'", L"跳著录", L"跳著稱'", L"跳著称", L"跳著者'", L"跳著者", L"跳著述'", L"跳著述", L"躊躇滿志'", L"踌躇滿志", L"踏著'", L"踏着", L"踏著書'", L"踏著书", L"踏著作'", L"踏著作", L"踏著名'", L"踏著名", L"踏著錄'", L"踏著录", L"踏著稱'", L"踏著称", L"踏著者'", L"踏著者", L"踏著述'", L"踏著述", L"踩著'", L"踩着", L"踩著書'", L"踩著书", L"踩著作'", L"踩著作", L"踩著名'", L"踩著名", L"踩著錄'", L"踩著录", L"踩著稱'", L"踩著称", L"踩著者'", L"踩著者", L"踩著述'", L"踩著述", L"身著'", L"身着", L"身著書'", L"身著书", L"身著作'", L"身著作", L"身著名'", L"身著名", L"身著錄'", L"身著录", L"身著稱'", L"身著称", L"身著者'", L"身著者", L"身著述'", L"身著述", L"躺著'", L"躺着", L"躺著書'", L"躺著书", L"躺著作'", L"躺著作", L"躺著名'", L"躺著名", L"躺著錄'", L"躺著录", L"躺著稱'", L"躺著称", L"躺著者'", L"躺著者", L"躺著述'", L"躺著述", L"轉著'", L"转着", L"轉著書'", L"转著书", L"轉著作'", L"转著作", L"轉著名'", L"转著名", L"轉著錄'", L"转著录", L"轉著稱'", L"转著称", L"轉著者'", L"转著者", L"轉著述'", L"转著述", L"載著'", L"载着", L"載著書'", L"载著书", L"載著作'", L"载著作", L"載著名'", L"载著名", L"載著錄'", L"载著录", L"載著稱'", L"载著称", L"載著者'", L"载著者", L"載著述'", L"载著述", L"較著'", L"较著", L"達著'", L"达着", L"達著書'", L"达著书", L"達著作'", L"达著作", L"達著名'", L"达著名", L"達著錄'", L"达著录", L"達著稱'", L"达著称", L"達著者'", L"达著者", L"達著述'", L"达著述", L"近角聪信'", L"近角聪信", L"近角聰信'", L"近角聪信", L"遠著'", L"远着", L"遠著書'", L"远著书", L"遠著作'", L"远著作", L"遠著名'", L"远著名", L"遠著錄'", L"远著录", L"遠著稱'", L"远著称", L"遠著者'", L"远著者", L"遠著述'", L"远著述", L"連著'", L"连着", L"連著書'", L"连著书", L"連著作'", L"连著作", L"連著名'", L"连著名", L"連著錄'", L"连著录", L"連著稱'", L"连著称", L"連著者'", L"连著者", L"連著述'", L"连著述", L"迫著'", L"迫着", L"追著'", L"追着", L"追著書'", L"追著书", L"追著作'", L"追著作", L"追著名'", L"追著名", L"追著錄'", L"追著录", L"追著稱'", L"追著称", L"追著者'", L"追著者", L"追著述'", L"追著述", L"逆著'", L"逆着", L"逆著書'", L"逆著书", L"逆著作'", L"逆著作", L"逆著名'", L"逆著名", L"逆著錄'", L"逆著录", L"逆著稱'", L"逆著称", L"逆著者'", L"逆著者", L"逆著述'", L"逆著述", L"逼著'", L"逼着", L"逼著書'", L"逼著书", L"逼著作'", L"逼著作", L"逼著名'", L"逼著名", L"逼著錄'", L"逼著录", L"逼著稱'", L"逼著称", L"逼著者'", L"逼著者", L"逼著述'", L"逼著述", L"遇著'", L"遇着", L"遇著書'", L"遇著书", L"遇著作'", L"遇著作", L"遇著名'", L"遇著名", L"遇著錄'", L"遇著录", L"遇著稱'", L"遇著称", L"遇著者'", L"遇著者", L"遇著述'", L"遇著述", L"遺著'", L"遗著", L"那麽'", L"那麽", L"郭子乾'", L"郭子乾", L"配著'", L"配着", L"配著書'", L"配著书", L"配著作'", L"配著作", L"配著名'", L"配著名", L"配著錄'", L"配著录", L"配著稱'", L"配著称", L"配著者'", L"配著者", L"配著述'", L"配著述", L"釀著'", L"酿着", L"釀著書'", L"酿著书", L"釀著作'", L"酿著作", L"釀著名'", L"酿著名", L"釀著錄'", L"酿著录", L"釀著稱'", L"酿著称", L"釀著者'", L"酿著者", L"釀著述'", L"酿著述", L"醯壺'", L"醯壶", L"醯壶'", L"醯壶", L"醯酱'", L"醯酱", L"醯醬'", L"醯酱", L"醯醋'", L"醯醋", L"醯醢'", L"醯醢", L"醯鸡'", L"醯鸡", L"醯雞'", L"醯鸡", L"重覆'", L"重复", L"金鍊'", L"金链", L"鐵鍊'", L"铁链", L"鉸鍊'", L"铰链", L"銀鍊'", L"银链", L"鋪著'", L"铺着", L"鋪著書'", L"铺著书", L"鋪著作'", L"铺著作", L"鋪著名'", L"铺著名", L"鋪著錄'", L"铺著录", L"鋪著稱'", L"铺著称", L"鋪著者'", L"铺著者", L"鋪著述'", L"铺著述", L"鍊子'", L"链子", L"鍊條'", L"链条", L"鍊鎖'", L"链锁", L"鍊錘'", L"链锤", L"鎖鍊'", L"锁链", L"鍾鍛'", L"锺锻", L"鍛鍾'", L"锻锺", L"閻懷禮'", L"闫怀礼", L"閉著'", L"闭着", L"閉著書'", L"闭著书", L"閉著作'", L"闭著作", L"閉著名'", L"闭著名", L"閉著錄'", L"闭著录", L"閉著稱'", L"闭著称", L"閉著者'", L"闭著者", L"閉著述'", L"闭著述", L"閑著'", L"闲着", L"閑著書'", L"闲著书", L"閑著作'", L"闲著作", L"閑著名'", L"闲著名", L"閑著錄'", L"闲著录", L"閑著稱'", L"闲著称", L"閑著者'", L"闲著者", L"閑著述'", L"闲著述", L"阿部正瞭'", L"阿部正瞭", L"附著'", L"附着", L"附睪'", L"附睾", L"附著書'", L"附著书", L"附著作'", L"附著作", L"附著名'", L"附著名", L"附著錄'", L"附著录", L"附著稱'", L"附著称", L"附著者'", L"附著者", L"附著述'", L"附著述", L"陋著'", L"陋着", L"陋著書'", L"陋著书", L"陋著作'", L"陋著作", L"陋著名'", L"陋著名", L"陋著錄'", L"陋著录", L"陋著稱'", L"陋著称", L"陋著者'", L"陋著者", L"陋著述'", L"陋著述", L"陪著'", L"陪着", L"陪著書'", L"陪著书", L"陪著作'", L"陪著作", L"陪著名'", L"陪著名", L"陪著錄'", L"陪著录", L"陪著稱'", L"陪著称", L"陪著者'", L"陪著者", L"陪著述'", L"陪著述", L"陳堵'", L"陳堵", L"陳禕'", L"陳禕", L"隨著'", L"随着", L"隨著書'", L"随著书", L"隨著作'", L"随著作", L"隨著名'", L"随著名", L"隨著錄'", L"随著录", L"隨著稱'", L"随著称", L"隨著者'", L"随著者", L"隨著述'", L"随著述", L"隔著'", L"隔着", L"隔著書'", L"隔著书", L"隔著作'", L"隔著作", L"隔著名'", L"隔著名", L"隔著錄'", L"隔著录", L"隔著稱'", L"隔著称", L"隔著者'", L"隔著者", L"隔著述'", L"隔著述", L"隱睪'", L"隱睾", L"雅著'", L"雅着", L"雅著書'", L"雅著书", L"雅著作'", L"雅著作", L"雅著名'", L"雅著名", L"雅著錄'", L"雅著录", L"雅著稱'", L"雅著称", L"雅著者'", L"雅著者", L"雅著述'", L"雅著述", L"雍乾'", L"雍乾", L"靠著'", L"靠着", L"靠著作'", L"靠著作", L"靠著名'", L"靠著名", L"靠著錄'", L"靠著录", L"靠著稱'", L"靠著称", L"靠著者'", L"靠著者", L"靠著述'", L"靠著述", L"頂著'", L"顶着", L"頂著書'", L"顶著书", L"頂著作'", L"顶著作", L"頂著名'", L"顶著名", L"頂著錄'", L"顶著录", L"頂著稱'", L"顶著称", L"頂著者'", L"顶著者", L"頂著述'", L"顶著述", L"項鍊'", L"项链", L"順著'", L"顺着", L"順著書'", L"顺著书", L"順著作'", L"顺著作", L"順著名'", L"顺著名", L"順著錄'", L"顺著录", L"順著稱'", L"顺著称", L"順著者'", L"顺著者", L"順著述'", L"顺著述", L"領著'", L"领着", L"領著書'", L"领著书", L"領著作'", L"领著作", L"領著名'", L"领著名", L"領著錄'", L"领著录", L"領著稱'", L"领著称", L"領著者'", L"领著者", L"領著述'", L"领著述", L"飄著'", L"飘着", L"飄著書'", L"飘著书", L"飄著作'", L"飘著作", L"飄著名'", L"飘著名", L"飄著錄'", L"飘著录", L"飄著稱'", L"飘著称", L"飄著者'", L"飘著者", L"飄著述'", L"飘著述", L"飭令'", L"飭令", L"駕著'", L"驾着", L"駕著書'", L"驾著书", L"駕著作'", L"驾著作", L"駕著名'", L"驾著名", L"駕著錄'", L"驾著录", L"駕著稱'", L"驾著称", L"駕著者'", L"驾著者", L"駕著述'", L"驾著述", L"罵著'", L"骂着", L"罵著書'", L"骂著书", L"罵著作'", L"骂著作", L"罵著名'", L"骂著名", L"罵著錄'", L"骂著录", L"罵著稱'", L"骂著称", L"罵著者'", L"骂著者", L"罵著述'", L"骂著述", L"騎著'", L"骑着", L"騎著書'", L"骑著书", L"騎著作'", L"骑著作", L"騎著名'", L"骑著名", L"騎著錄'", L"骑著录", L"騎著稱'", L"骑著称", L"騎著者'", L"骑著者", L"騎著述'", L"骑著述", L"騙著'", L"骗着", L"騙著書'", L"骗著书", L"騙著作'", L"骗著作", L"騙著名'", L"骗著名", L"騙著錄'", L"骗著录", L"騙著稱'", L"骗著称", L"騙著者'", L"骗著者", L"騙著述'", L"骗著述", L"高著'", L"高着", L"高著書'", L"高著书", L"高著作'", L"高著作", L"高著名'", L"高著名", L"高著錄'", L"高著录", L"高著稱'", L"高著称", L"高著者'", L"高著者", L"高著述'", L"高著述", L"髭著'", L"髭着", L"髭著書'", L"髭著书", L"髭著作'", L"髭著作", L"髭著名'", L"髭著名", L"髭著錄'", L"髭著录", L"髭著稱'", L"髭著称", L"髭著者'", L"髭著者", L"髭著述'", L"髭著述", L"鬱姓'", L"鬱姓", L"鬱氏'", L"鬱氏", L"魏徵'", L"魏徵", L"魚乾乾'", L"鱼干干", L"鯰魚'", L"鲶鱼", L"麯崇裕'", L"麯崇裕", L"麴義'", L"麴义", L"麴义'", L"麴义", L"麴英'", L"麴英", L"麽氏'", L"麽氏", L"麽麽'", L"麽麽", L"麼麼'", L"麽麽", L"黏著'", L"黏着", L"黏著書'", L"黏著书", L"黏著作'", L"黏著作", L"黏著名'", L"黏著名", L"黏著錄'", L"黏著录", L"黏著稱'", L"黏著称", L"黏著者'", L"黏著者", L"黏著述'", L"黏著述", L"覆蓋", L"覆盖", L"尼日", L"尼日", L"公車上書", L"公车上书", L"瞭望", L"瞭望", L"電線上", L"电线上", L"遠程控制", L"远程控制", L"東加拿大", L"东加拿大", L"東加里曼丹", L"东加里曼丹", L"東加勒比元", L"东加勒比元", L"股東", L"股东", L"莱福士", L"莱福士", L"理查德", L"理查德", L"戴安娜", L"戴安娜", L"希拉里", L"希拉里", L"希拉克", L"希拉克", L"巴斯德", L"巴斯德", L"哪吒", L"哪吒", L"棡", L"", L"韓復榘", L"韩复榘", L"陳奕迅", L"陈奕迅", L"陈奕迅", L"陈奕迅", L"拿破崙", L"拿破仑", L"范瑋琪", L"范玮琪", L"湯加麗", L"汤加丽", L"普里查德", L"普里查德", L"班达拉奈克", L"班达拉奈克", L"班達拉奈克", L"班达拉奈克", L"吳其濬", L"吴其濬", L"吴其濬", L"吴其濬", L"魏徵", L"魏徵", L"柳詒徵", L"柳诒徵", L"柳诒徵", L"柳诒徵", L"於姓", L"於姓", L"於氏", L"於氏", L"於夫羅", L"於夫罗", L"於夫罗", L"於夫罗", L"於梨華", L"於梨华", L"於梨华", L"於梨华", L"於祥玉", L"於祥玉", L"於潜县", L"於潜县", L"於潛縣", L"於潜县", L"於崇文", L"於崇文", L"數碼港", L"数码港", L"五箇山", L"五箇山", L"崑崙", L"昆仑", L"嶽麓山", L"岳麓山", L"交阯", L"交阯", L"石碁镇", L"石碁镇", L"石碁鎮", L"石碁镇", L"哈薩克", L"哈萨克", L"烏茲別克", L"乌兹别克", L"土庫曼", L"土库曼", L"吉爾吉斯", L"吉尔吉斯", L"塔吉克", L"塔吉克", L"加沙地帶", L"加沙地带", L"教廷", L"教廷", L"梵蒂岡", L"梵蒂冈", L"尼日尔", L"尼日尔", L"尼日利亚", L"尼日利亚", L"獅子山", L"狮子山", L"肯雅塔", L"肯雅塔", L"荷李活", L"荷李活", L"多米尼克", L"多米尼克", L"蒙卡達", L"蒙卡达", L"台灣新力國際", L"台湾新力国际", L"新力博德曼", L"新力博德曼", L"新力電腦娛樂", L"新力电脑娱乐", L"足球俱樂部", L"足球俱乐部", L"幺半群", L"幺半群", L"幺元", L"幺元", L"數位", L"数位", L"上傳", L"上传", L"視窗", L"视窗", L"解碼", L"解码", L"模擬", L"模拟", L"離線", L"离线", L"電腦", L"电脑", L"计算機", L"计算机", L"光碟", L"光碟", L"碟片", L"碟片", L"互聯網", L"互联网", L"網際網絡", L"网际网络", L"红旗软件", L"红旗软件", L"紅旗軟件", L"红旗软件", L"金山软件", L"金山软件", L"金山軟件", L"金山软件", L"全景软体", L"全景软体", L"全景軟體", L"全景软体", L"熊猫软体", L"熊猫软体", L"熊貓軟體", L"熊猫软体", L"软体电脑", L"软体电脑", L"軟體電腦", L"软体电脑", L"沈積", L"沉积", L"維基共享資源", L"维基共享资源", L"外部連結", L"外部链接", L"外部鏈結", L"外部链接", L"檔案存廢討論", L"文件存废讨论", L"捷徑重定向", L"快捷方式重定向", L"捷徑列表", L"快捷方式列表", L"福斯", L"福斯", L"暱稱", L"昵称", L"電器", L"电器", L"通道", L"通道", L"鍊金", L"炼金", L"軟體動物", L"软体动物", L"蜑家", L"蛋家", L"胺基酸", L"氨基酸", L"水氣", L"水汽", L"雪糕", L"雪糕", L"方程式", L"方程式", L"巴士", L"巴士", L"手電筒", L"手电筒", L"保安", L"保安", L"保全", L"保全", L"慣用", L"惯用", L"丑年", L"丑年", L"涅槃", L"涅槃", L"宏碁", L"宏碁", L"碁聖", L"碁圣", L"住房", L"住房", L"摄影集", L"摄影集", L"計畫", L"计划", L"籍貫", L"籍贯", L"鬱姓", L"鬱姓", L"進位制", L"进位制", L"睪", L"睾", L"鄉愿", L"乡愿", L"乡愿", L"乡愿", L"捲舌", L"卷舌", L"射鵰", L"射雕", L"神鵰", L"神雕", L"醣", L"糖", L"沈船", L"沉船", L"醯胺", L"酰胺", L"电影集团", L"电影集团", L"電影集團", L"电影集团", L"侏儸紀", L"侏罗纪", L"甚麽", L"什么", L"甚麼", L"什么", L"乾陵", L"乾陵", L"乾县", L"乾县", L"乾縣", L"乾县", L"乾隆", L"乾隆", L"康乾", L"康乾", L"乾嘉", L"乾嘉", L"乾盛世", L"乾盛世", L"郭子乾", L"郭子乾", L"張法乾", L"张法乾", L"蕭乾", L"萧乾", L"萧乾", L"萧乾", L"乾旦", L"乾旦", L"乾斷", L"乾断", L"乾断", L"乾断", L"乾圖", L"乾图", L"乾图", L"乾图", L"乾綱", L"乾纲", L"乾纲", L"乾纲", L"乾坤", L"乾坤", L"乾紅", L"乾红", L"乾红", L"乾红", L"乾乾", L"乾乾", L"乾清宮", L"乾清宫", L"乾清宫", L"乾清宫", L"乾象", L"乾象", L"乾宅", L"乾宅", L"乾造", L"乾造", L"乾曜", L"乾曜", L"乾元", L"乾元", L"乾卦", L"乾卦", L"李乾德", L"李乾德", L"尼乾陀", L"尼乾陀", L"藉", L"藉", L"憑藉", L"凭借", L"藉端", L"借端", L"藉故", L"借故", L"藉口", L"借口", L"藉助", L"借助", L"藉手", L"借手", L"藉詞", L"借词", L"藉機", L"借机", L"藉此", L"借此", L"藉由", L"借由", L"那麼", L"那么", L"獃", L"呆", L"呆", L"呆", L"卡达西", L"卡达西", L"卡達西", L"卡达西", L"薹", L"薹", L"』", L"’", L"『", L"‘", L"」", L"”", L"「", L"“", L"｢", L"“", L"｣", L"”", L"𪘀", L"𪚏", L"𪋿", L"𪎍", L"𪈼", L"𪉓", L"𪈁", L"𪉕", L"𪄕", L"𪉒", L"𪄆", L"𪉔", L"𪃏", L"𪉏", L"𪃍", L"𪉐", L"𪁖", L"𪉌", L"𪁈", L"𪉉", L"𪀾", L"𪉋", L"𪀸", L"𪉅", L"𩿪", L"𪉄", L"𩽇", L"𩾎", L"𩸦", L"𩾆", L"𩸃", L"𩾅", L"𩷰", L"𩾄", L"𩶱", L"𩽽", L"𩶰", L"𩽿", L"𩶘", L"䲞", L"𩵹", L"𩽻", L"𩵩", L"𩽺", L"𩨂", L"骢", L"𩧆", L"𩨐", L"𩥑", L"𩨌", L"𩥉", L"𩧱", L"𩥇", L"𩨍", L"𩥄", L"𩨋", L"𩤸", L"𩨅", L"𩤲", L"𩨉", L"𩤙", L"𩨆", L"𩤊", L"𩧩", L"𩣺", L"𩧼", L"𩣑", L"䯃", L"𩣏", L"𩧶", L"𩢾", L"𩧮", L"𩢸", L"𩧳", L"𩢴", L"𩧵", L"𩢡", L"𩧬", L"𩡺", L"𩧦", L"𩞯", L"䭪", L"𩞦", L"𩠏", L"𩞄", L"𩠎", L"𩝔", L"𩠋", L"𩜵", L"𩠊", L"𩜦", L"𩠆", L"𩜇", L"𩠉", L"𩛩", L"𩠃", L"𩛆", L"𩠂", L"𩚵", L"𩠁", L"𩚥", L"𩠀", L"𩚛", L"𩟿", L"𩙈", L"𩙰", L"𩘺", L"𩙬", L"𩘹", L"𩙨", L"𩘝", L"𩙭", L"𩘀", L"𩙩", L"𩗗", L"飓", L"𩗀", L"𩙦", L"𩓣", L"𩖕", L"𩏪", L"𩏽", L"𩎢", L"𩏾", L"𨷲", L"𨸎", L"𨶲", L"𨸋", L"𨶮", L"𨸌", L"𨶏", L"𨸊", L"𨳕", L"𨸀", L"𨳑", L"𨸁", L"𨯅", L"䥿", L"𨫒", L"𨱐", L"𨧱", L"𨱊", L"𨧜", L"䦁", L"𨦫", L"䦀", L"𨥛", L"𨱀", L"𨥊", L"𨰿", L"𨏥", L"𨐊", L"𨏠", L"𨐇", L"𨎮", L"𨐉", L"𨋢", L"䢂", L"𨊻", L"𨐆", L"𨊸", L"䢁", L"𨊰", L"䢀", L"𧹔", L"账", L"𧶧", L"䞎", L"𧶔", L"𧹓", L"𧵳", L"䞌", L"𧩙", L"䜥", L"𧝞", L"䘛", L"𧜵", L"䙊", L"𦂅", L"𦈒", L"𥿊", L"𦈈", L"𤺥", L"瘩", L"𤫩", L"㻏", L"𤪺", L"㻘", L"𡢃", L"㛠", L"𡠹", L"㛿", L"𡞵", L"㛟", L"鷴", L"鹇", L"鰛", L"鳁", L"鯰", L"鲶", L"靉", L"叆", L"靆", L"叇", L"闢", L"辟", L"钂", L"镋", L"鉆", L"钻", L"鈅", L"钥", L"讐", L"雠", L"讋", L"詟", L"証", L"证", L"襴", L"襕", L"襬", L"摆", L"蘀", L"萚", L"薴", L"苧", L"薑", L"姜", L"膕", L"腘", L"翽", L"翙", L"翬", L"翚", L"纇", L"颣", L"籤", L"签", L"籛", L"篯", L"礄", L"硚", L"磠", L"硵", L"璸", L"瑸", L"璵", L"玙", L"璡", L"琎", L"灩", L"滟", L"濜", L"浕", L"漊", L"溇", L"溳", L"涢", L"溮", L"浉", L"渢", L"沨", L"樿", L"椫", L"槶", L"椢", L"槮", L"椮", L"捲", L"卷", L"廎", L"庼", L"夥", L"伙", L"囉", L"啰", L"嘓", L"啯", L"俥", L"伡", L"䴴", L"𪎋", L"䴬", L"𪎈", L"䴉", L"鹮", L"䲰", L"𪉂", L"䲘", L"鳤", L"䲖", L"𩾂", L"䲁", L"鳚", L"䱽", L"䲝", L"䱰", L"𩾋", L"䱬", L"𩾊", L"䱙", L"𩾈", L"䰾", L"鲃", L"䯀", L"䯅", L"䮾", L"𩧪", L"䮳", L"𩨏", L"䮫", L"𩨇", L"䮠", L"𩧿", L"䮞", L"𩨁", L"䮝", L"𩧰", L"䭿", L"𩧭", L"䭃", L"𩠈", L"䭀", L"𩠇", L"䬞", L"𩙧", L"䬝", L"𩙯", L"䬘", L"𩙮", L"䫴", L"𩖗", L"䪘", L"𩏿", L"䪗", L"𩐀", L"䪏", L"𩏼", L"䦟", L"䦷", L"䦛", L"䦶", L"䦘", L"𨸄", L"䥱", L"䥾", L"䥇", L"䦂", L"䝼", L"䞍", L"䝻", L"𧹕", L"䙡", L"䙌", L"䎱", L"䎬", L"䌰", L"𦈙", L"䌥", L"𦈠", L"䌟", L"𦈞", L"䌝", L"𦈟", L"䌖", L"𦈜", L"䌋", L"𦈘", L"䌈", L"𦈖", L"䋿", L"𦈓", L"䋻", L"䌾", L"䋹", L"䌿", L"䋚", L"䌻", L"䋙", L"䌺", L"䊷", L"䌶", L"㩳", L"㧐", L"㩜", L"㨫", L"㥮", L"㤘", L"㠏", L"㟆", L"㘚", L"㘎", L"㑶", L"㐹", L"㑳", L"㑇", L"騧", L"䯄", L"昇", L"升", L"崙", L"仑", L"鹼", L"碱", L"阪", L"阪", L"閒", L"闲", L"鑀", L"锿", L"鍊", L"链", L"錼", L"镎", L"鋂", L"镅", L"鈽", L"钚", L"醯", L"酰", L"覆", L"覆", L"著", L"著", L"礆", L"碱", L"矽", L"硅", L"痲", L"痳", L"畫", L"画", L"拾", L"拾", L"幺", L"幺", L"埠", L"埠", L"后", L"后", L"余", L"余", L"龢", L"和", L"龞", L"鳖", L"龎", L"庞", L"齩", L"咬", L"齧", L"啮", L"齇", L"齄", L"鼦", L"貂", L"鼈", L"鳖", L"鼇", L"鳌", L"鼃", L"蛙", L"麪", L"面", L"麤", L"粗", L"麞", L"獐", L"麕", L"麇", L"麐", L"麟", L"麅", L"狍", L"麁", L"粗", L"鹻", L"碱", L"鸎", L"莺", L"鷰", L"燕", L"鵶", L"鸦", L"鵰", L"雕", L"鵞", L"鹅", L"鴈", L"雁", L"鱻", L"鲜", L"鱓", L"鳝", L"鱏", L"鲟", L"鰌", L"鳅", L"魊", L"蜮", L"鬴", L"釜", L"鬰", L"郁", L"鬭", L"斗", L"鬪", L"斗", L"鬨", L"哄", L"鬦", L"斗", L"鬉", L"鬃", L"鬂", L"鬓", L"鬁", L"痢", L"鬀", L"剃", L"髴", L"佛", L"髩", L"鬓", L"髥", L"髯", L"髣", L"仿", L"髠", L"髡", L"髈", L"膀", L"骾", L"鲠", L"骔", L"鬃", L"驘", L"骡", L"騣", L"鬃", L"騗", L"骗", L"騐", L"验", L"騌", L"鬃", L"駮", L"驳", L"駞", L"驼", L"駈", L"驱", L"饟", L"饷", L"饝", L"馍", L"饍", L"膳", L"餽", L"馈", L"餻", L"糕", L"餹", L"糖", L"餵", L"喂", L"餬", L"糊", L"餧", L"喂", L"餈", L"糍", L"餁", L"饪", L"飺", L"糍", L"飱", L"飧", L"飤", L"饲", L"飜", L"翻", L"飚", L"飙", L"飈", L"飙", L"飃", L"飘", L"颕", L"颖", L"顦", L"憔", L"顋", L"腮", L"頼", L"赖", L"頴", L"颖", L"頟", L"额", L"韮", L"韭", L"韤", L"袜", L"韝", L"鞲", L"韈", L"袜", L"鞵", L"鞋", L"鞌", L"鞍", L"鞉", L"鼗", L"靱", L"韧", L"靭", L"韧", L"靣", L"面", L"霤", L"溜", L"霛", L"灵", L"雝", L"雍", L"隷", L"隶", L"隣", L"邻", L"隟", L"隙", L"隖", L"坞", L"隄", L"堤", L"隂", L"阴", L"陻", L"堙", L"陗", L"峭", L"阯", L"址", L"阬", L"坑", L"阨", L"厄", L"闝", L"嫖", L"闚", L"窥", L"関", L"关", L"閙", L"闹", L"鑵", L"罐", L"鑤", L"刨", L"鑛", L"矿", L"鑚", L"钻", L"鑑", L"鉴", L"鏇", L"旋", L"鎻", L"锁", L"鎚", L"锤", L"鎗", L"枪", L"鎌", L"镰", L"鍼", L"针", L"鍳", L"鉴", L"鍫", L"锹", L"銲", L"焊", L"銕", L"铁", L"鉏", L"锄", L"鉋", L"刨", L"鉅", L"巨", L"鈆", L"铅", L"釬", L"焊", L"釦", L"扣", L"醿", L"醾", L"醼", L"宴", L"醻", L"酬", L"醕", L"醇", L"醃", L"腌", L"酧", L"酬", L"郉", L"邢", L"遶", L"绕", L"遯", L"遁", L"遡", L"溯", L"遉", L"侦", L"逷", L"逖", L"逬", L"迸", L"逩", L"奔", L"逥", L"回", L"逈", L"迥", L"迻", L"移", L"迯", L"逃", L"迖", L"达", L"迆", L"迤", L"辴", L"冁", L"辳", L"农", L"辤", L"辞", L"辢", L"辣", L"辠", L"罪", L"輭", L"软", L"輙", L"辄", L"躶", L"裸", L"躳", L"躬", L"躱", L"躲", L"躭", L"耽", L"蹵", L"蹴", L"蹧", L"糟", L"蹟", L"迹", L"蹔", L"暂", L"蹓", L"溜", L"蹏", L"蹄", L"踰", L"逾", L"踡", L"蜷", L"踁", L"胫", L"跴", L"踩", L"跥", L"跺", L"趦", L"趑", L"趂", L"趁", L"贑", L"贛", L"賸", L"剩", L"賷", L"赍", L"賛", L"赞", L"賉", L"恤", L"貛", L"獾", L"貍", L"狸", L"豔", L"艳", L"豓", L"艳", L"谘", L"咨", L"讚", L"赞", L"讌", L"宴", L"讅", L"审", L"讁", L"谪", L"譭", L"毁", L"譟", L"噪", L"譔", L"撰", L"譌", L"讹", L"譆", L"嘻", L"譁", L"哗", L"謿", L"嘲", L"謩", L"谟", L"謌", L"歌", L"諮", L"咨", L"諡", L"谥", L"諠", L"喧", L"諐", L"愆", L"誖", L"悖", L"詧", L"察", L"託", L"托", L"觧", L"解", L"觝", L"抵", L"觔", L"斤", L"覰", L"觑", L"覩", L"睹", L"覔", L"觅", L"覊", L"羁", L"覈", L"核", L"覇", L"霸", L"襍", L"杂", L"襉", L"裥", L"襆", L"幞", L"襃", L"褒", L"褭", L"袅", L"褃", L"裉", L"裩", L"裈", L"裠", L"裙", L"裌", L"夹", L"袵", L"衽", L"衺", L"邪", L"衞", L"卫", L"衖", L"弄", L"衒", L"炫", L"衇", L"脉", L"衂", L"衄", L"蠭", L"蜂", L"蠧", L"蠹", L"蠔", L"蚝", L"蠒", L"茧", L"蠏", L"蟹", L"蠍", L"蝎", L"蟇", L"蟆", L"蟁", L"蚊", L"螾", L"蚓", L"螡", L"蚊", L"螙", L"蠹", L"螘", L"蚁", L"螎", L"融", L"蝱", L"虻", L"蝯", L"猿", L"蝨", L"虱", L"蜺", L"霓", L"蜯", L"蚌", L"蜨", L"蝶", L"蜖", L"蛔", L"蜋", L"螂", L"蛕", L"蛔", L"蚦", L"蚺", L"蚡", L"鼢", L"蚘", L"蛔", L"虵", L"蛇", L"虖", L"呼", L"蘤", L"花", L"蘓", L"苏", L"蘐", L"萱", L"蘂", L"蕊", L"藼", L"萱", L"藷", L"薯", L"蕿", L"萱", L"蕚", L"萼", L"蕋", L"蕊", L"蕅", L"藕", L"蔥", L"葱", L"蔘", L"参", L"蔕", L"蒂", L"蔆", L"菱", L"蓱", L"萍", L"蓧", L"莜", L"蓡", L"參", L"蓆", L"席", L"葠", L"参", L"萲", L"萱", L"菸", L"烟", L"菴", L"庵", L"菢", L"抱", L"菓", L"果", L"莕", L"荇", L"荳", L"豆", L"荅", L"答", L"茘", L"荔", L"苺", L"莓", L"芲", L"花", L"艪", L"橹", L"艣", L"橹", L"艢", L"樯", L"舩", L"船", L"舝", L"辖", L"舘", L"馆", L"舖", L"铺", L"舃", L"舄", L"臯", L"皋", L"臥", L"卧", L"臝", L"裸", L"臙", L"胭", L"臕", L"膘", L"臋", L"臀", L"臈", L"腊", L"膓", L"肠", L"膆", L"嗉", L"腁", L"胼", L"脣", L"唇", L"脗", L"吻", L"脇", L"胁", L"脃", L"脆", L"胷", L"胸", L"肬", L"疣", L"肧", L"胚", L"肐", L"胳", L"肎", L"肯", L"聼", L"听", L"耡", L"锄", L"耉", L"耇", L"耈", L"耇", L"翶", L"翱", L"翫", L"玩", L"翄", L"翅", L"羶", L"膻", L"羴", L"膻", L"羣", L"群", L"羢", L"绒", L"羗", L"羌", L"罸", L"罚", L"罣", L"挂", L"罏", L"垆", L"罎", L"坛", L"罈", L"坛", L"罇", L"樽", L"罁", L"缸", L"缾", L"瓶", L"繦", L"襁", L"繖", L"伞", L"繈", L"襁", L"縚", L"绦", L"縂", L"总", L"緥", L"褓", L"緜", L"绵", L"緐", L"繁", L"綵", L"彩", L"綑", L"捆", L"絸", L"茧", L"絏", L"绁", L"絃", L"弦", L"紮", L"扎", L"紥", L"扎", L"糱", L"糵", L"糉", L"粽", L"粧", L"妆", L"粦", L"磷", L"粃", L"秕", L"籨", L"奁", L"籐", L"藤", L"簷", L"檐", L"簮", L"簪", L"簒", L"篡", L"簑", L"蓑", L"簆", L"筘", L"篲", L"彗", L"篠", L"筱", L"篛", L"箬", L"箚", L"札", L"箒", L"帚", L"箎", L"篪", L"箇", L"个", L"筯", L"箸", L"筭", L"算", L"筩", L"筒", L"筞", L"策", L"竾", L"篪", L"竢", L"俟", L"竝", L"并", L"竚", L"伫", L"竒", L"奇", L"窻", L"窗", L"窰", L"窑", L"窓", L"窗", L"穽", L"阱", L"穨", L"颓", L"穤", L"糯", L"穉", L"稚", L"穅", L"糠", L"稾", L"稿", L"稺", L"稚", L"稭", L"秸", L"稬", L"糯", L"稜", L"棱", L"稉", L"粳", L"秖", L"祇", L"秔", L"粳", L"秌", L"秋", L"秊", L"年", L"禩", L"祀", L"祘", L"算", L"祕", L"秘", L"礶", L"罐", L"礮", L"炮", L"磟", L"碌", L"磎", L"溪", L"碪", L"砧", L"碕", L"埼", L"碁", L"棋", L"硏", L"研", L"砲", L"炮", L"矴", L"碇", L"矙", L"瞰", L"矁", L"瞅", L"瞖", L"翳", L"瞇", L"眯", L"睠", L"眷", L"眡", L"视", L"眎", L"视", L"盪", L"荡", L"盌", L"碗", L"盋", L"钵", L"盇", L"盍", L"皷", L"鼓", L"皜", L"皓", L"皐", L"皋", L"皃", L"貌", L"皁", L"皂", L"癡", L"痴", L"癒", L"愈", L"癅", L"瘤", L"瘖", L"喑", L"瘉", L"愈", L"痺", L"痹", L"痠", L"酸", L"痐", L"蛔", L"痌", L"恫", L"疿", L"痱", L"疎", L"疏", L"疉", L"叠", L"畱", L"留", L"畮", L"亩", L"畧", L"略", L"畣", L"答", L"畡", L"垓", L"畊", L"耕", L"畆", L"亩", L"甽", L"圳", L"甦", L"苏", L"甞", L"尝", L"甖", L"罂", L"甕", L"瓮", L"甎", L"砖", L"甆", L"瓷", L"瓌", L"瑰", L"瓈", L"璃", L"璿", L"璇", L"璢", L"琉", L"瑯", L"琅", L"瑠", L"琉", L"瑇", L"玳", L"琹", L"琴", L"琱", L"雕", L"琖", L"盏", L"珮", L"佩", L"珪", L"圭", L"玅", L"妙", L"獧", L"狷", L"獘", L"毙", L"獏", L"貘", L"猨", L"猿", L"猂", L"悍", L"狥", L"徇", L"犂", L"犁", L"牴", L"抵", L"牠", L"它", L"牕", L"窗", L"牓", L"榜", L"牐", L"闸", L"牎", L"窗", L"牋", L"笺", L"牀", L"床", L"爗", L"烨", L"爕", L"燮", L"爊", L"熬", L"燿", L"耀", L"燻", L"熏", L"燐", L"磷", L"燄", L"焰", L"熈", L"熙", L"煠", L"炸", L"煗", L"暖", L"煖", L"暖", L"煕", L"熙", L"煑", L"煮", L"煇", L"辉", L"烱", L"炯", L"烖", L"灾", L"炤", L"照", L"灨", L"赣", L"灋", L"法", L"濶", L"阔", L"濬", L"浚", L"濇", L"涩", L"澣", L"浣", L"澁", L"涩", L"潠", L"噀", L"潄", L"漱", L"滛", L"淫", L"滙", L"汇", L"溼", L"湿", L"湼", L"涅", L"湻", L"淳", L"湧", L"涌", L"湌", L"餐", L"淛", L"浙", L"涖", L"莅", L"浄", L"净", L"洩", L"泄", L"泝", L"溯", L"沍", L"冱", L"汚", L"污", L"氷", L"冰", L"氊", L"毡", L"毧", L"绒", L"毘", L"毗", L"毉", L"医", L"殭", L"僵", L"殀", L"夭", L"歴", L"历", L"歛", L"敛", L"歘", L"欻", L"歗", L"啸", L"歎", L"叹", L"欵", L"款", L"欑", L"攒", L"欎", L"郁", L"櫺", L"棂", L"櫈", L"凳", L"櫂", L"棹", L"檾", L"苘", L"檝", L"楫", L"檇", L"槜", L"橰", L"槔", L"橤", L"蕊", L"橜", L"橛", L"樝", L"楂", L"樑", L"梁", L"樐", L"橹", L"槼", L"規", L"槹", L"槔", L"槪", L"概", L"槩", L"概", L"槕", L"桌", L"槓", L"杠", L"槃", L"盘", L"槀", L"槁", L"榦", L"干", L"榘", L"矩", L"楳", L"梅", L"椾", L"笺", L"椶", L"棕", L"椗", L"碇", L"椉", L"乘", L"椀", L"碗", L"棊", L"棋", L"棃", L"梨", L"梹", L"槟", L"桺", L"柳", L"桮", L"杯", L"桚", L"拶", L"桒", L"桑", L"栰", L"筏", L"栢", L"柏", L"栞", L"刊", L"栔", L"契", L"栁", L"柳", L"査", L"查", L"柺", L"拐", L"柹", L"柿", L"枴", L"拐", L"枱", L"台", L"枒", L"桠", L"杇", L"圬", L"朶", L"朵", L"朢", L"望", L"朞", L"期", L"曡", L"叠", L"暱", L"昵", L"暠", L"皓", L"暎", L"映", L"晳", L"晰", L"昰", L"是", L"昬", L"昏", L"旾", L"春", L"旹", L"时", L"旤", L"祸", L"旣", L"既", L"旛", L"幡", L"旂", L"旗", L"斚", L"斝", L"斈", L"学", L"敺", L"驱", L"敭", L"扬", L"敎", L"教", L"敍", L"叙", L"敂", L"叩", L"敁", L"掂", L"攷", L"考", L"攩", L"挡", L"擧", L"举", L"擣", L"捣", L"擡", L"抬", L"擕", L"携", L"撴", L"蹾", L"撧", L"撅", L"撦", L"扯", L"撢", L"掸", L"摃", L"扛", L"摀", L"捂", L"搾", L"榨", L"搯", L"掏", L"搨", L"拓", L"搧", L"扇", L"搥", L"捶", L"搤", L"扼", L"搉", L"榷", L"搇", L"揿", L"搆", L"构", L"揹", L"背", L"揷", L"插", L"揫", L"揪", L"揑", L"捏", L"揌", L"塞", L"揅", L"研", L"掽", L"碰", L"捄", L"救", L"挵", L"弄", L"挱", L"挲", L"挌", L"格", L"拕", L"拖", L"拑", L"钳", L"抴", L"曳", L"抝", L"拗", L"扡", L"拖", L"戼", L"卯", L"戹", L"厄", L"戱", L"戏", L"戞", L"戛", L"戉", L"钺", L"懽", L"欢", L"懃", L"勤", L"憖", L"慭", L"憇", L"憩", L"慾", L"欲", L"慽", L"戚", L"慼", L"戚", L"慴", L"慑", L"慠", L"傲", L"慙", L"惭", L"慇", L"殷", L"慂", L"恿", L"愽", L"博", L"愬", L"诉", L"惷", L"蠢", L"惪", L"德", L"惥", L"恿", L"悽", L"凄", L"悮", L"误", L"悤", L"匆", L"悞", L"误", L"恡", L"吝", L"恠", L"怪", L"恉", L"旨", L"怳", L"恍", L"怱", L"匆", L"徬", L"彷", L"徧", L"遍", L"徃", L"往", L"彿", L"佛", L"彫", L"雕", L"彜", L"彝", L"彔", L"录", L"彊", L"强", L"弔", L"吊", L"弍", L"贰", L"弌", L"壹", L"廼", L"迺", L"廻", L"回", L"廹", L"迫", L"廸", L"迪", L"廵", L"巡", L"廕", L"荫", L"廐", L"厩", L"庽", L"寓", L"庻", L"庶", L"幷", L"并", L"幚", L"帮", L"幙", L"幕", L"幑", L"徽", L"幎", L"幂", L"幈", L"屏", L"幇", L"帮", L"帬", L"裙", L"帋", L"纸", L"帀", L"匝", L"巹", L"卺", L"巵", L"卮", L"巗", L"岩", L"嶨", L"峃", L"嶃", L"崭", L"嵗", L"岁", L"嵒", L"岩", L"崧", L"嵩", L"崕", L"崖", L"峯", L"峰", L"峩", L"峨", L"峝", L"峒", L"岅", L"坂", L"屭", L"屃", L"屛", L"屏", L"尲", L"尴", L"尫", L"尪", L"尩", L"尪", L"尠", L"鲜", L"尟", L"鲜", L"尙", L"尚", L"尒", L"尔", L"尅", L"克", L"寳", L"宝", L"寕", L"宁", L"寑", L"寝", L"寃", L"冤", L"寀", L"采", L"宼", L"寇", L"宂", L"冗", L"孼", L"孽", L"孶", L"孳", L"孃", L"娘", L"嬾", L"懒", L"嬭", L"奶", L"嬝", L"袅", L"嫺", L"娴", L"嫰", L"嫩", L"嫋", L"袅", L"媿", L"愧", L"媫", L"婕", L"媍", L"妇", L"婬", L"淫", L"婣", L"姻", L"姸", L"妍", L"姪", L"侄", L"姙", L"妊", L"姉", L"姊", L"妬", L"妒", L"奨", L"奖", L"夘", L"卯", L"夀", L"寿", L"壻", L"婿", L"壜", L"坛", L"壖", L"堧", L"壎", L"埙", L"壄", L"野", L"墪", L"墩", L"墖", L"塔", L"塼", L"砖", L"塲", L"场", L"塡", L"填", L"塟", L"葬", L"堦", L"階", L"堘", L"塍", L"埳", L"坎", L"埜", L"野", L"垜", L"垛", L"垇", L"坳", L"坿", L"附", L"坵", L"丘", L"圝", L"圞", L"圅", L"函", L"囯", L"国", L"囬", L"回", L"囙", L"因", L"囘", L"回", L"囓", L"啮", L"嚐", L"尝", L"噑", L"嗥", L"噐", L"器", L"噉", L"啖", L"嘠", L"嘎", L"嗁", L"啼", L"喫", L"吃", L"喦", L"岩", L"喒", L"咱", L"啣", L"衔", L"啗", L"啖", L"啎", L"忤", L"唸", L"念", L"唘", L"启", L"唕", L"唣", L"咷", L"啕", L"咊", L"和", L"呪", L"咒", L"呌", L"叫", L"吿", L"告", L"吢", L"吣", L"吚", L"咿", L"叡", L"睿", L"叜", L"叟", L"叚", L"假", L"収", L"收", L"厰", L"厂", L"厯", L"历", L"厫", L"廒", L"厤", L"历", L"厐", L"庞", L"卽", L"即", L"卹", L"恤", L"匵", L"椟", L"匳", L"奁", L"匲", L"奁", L"匊", L"掬", L"匄", L"丐", L"匃", L"丐", L"勳", L"勋", L"勦", L"剿", L"勠", L"戮", L"勗", L"勖", L"勌", L"倦", L"勅", L"敕", L"効", L"效", L"劒", L"剑", L"劄", L"札", L"剹", L"戮", L"剷", L"铲", L"剳", L"劄", L"剙", L"創", L"剏", L"创", L"刼", L"劫", L"刴", L"剁", L"刧", L"劫", L"刦", L"劫", L"凴", L"凭", L"処", L"处", L"凣", L"凡", L"冺", L"泯", L"冐", L"冒", L"冄", L"冉", L"兠", L"兜", L"兎", L"兔", L"儹", L"攒", L"儵", L"倏", L"儌", L"侥", L"儁", L"俊", L"僱", L"雇", L"働", L"動", L"僊", L"仙", L"傚", L"效", L"偺", L"咱", L"偪", L"逼", L"偘", L"侃", L"倸", L"睬", L"倣", L"仿", L"倖", L"幸", L"倐", L"倏", L"倂", L"并", L"俻", L"备", L"侷", L"局", L"佔", L"占", L"伕", L"夫", L"亾", L"亡", L"亷", L"廉", L"亱", L"夜", L"亯", L"享", L"亝", L"斋", L"亁", L"乾", L"乹", L"乾", L"乗", L"乘", L"両", L"两", L"䶊", L"衄", L"䵞", L"黥", L"䴸", L"麸", L"䳘", L"鹅", L"䲣", L"渔", L"䱷", L"渔", L"䰟", L"魂", L"䰖", L"纂", L"䭾", L"驮", L"䬸", L"餐", L"䬃", L"飒", L"䪿", L"囟", L"䧡", L"墉", L"䥥", L"镰", L"䣛", L"膝", L"䠶", L"射", L"䠥", L"蹩", L"䝔", L"獾", L"䛡", L"话", L"䛐", L"词", L"䘚", L"卒", L"䘑", L"脉", L"䘏", L"恤", L"䗬", L"蜂", L"䖏", L"处", L"䑲", L"棹", L"䌽", L"彩", L"䊀", L"糊", L"䈰", L"筲", L"䃠", L"碹", L"㿜", L"瘪", L"㽞", L"留", L"㼝", L"碗", L"㴱", L"深", L"㳒", L"法", L"㳄", L"涎", L"㳂", L"沿", L"㱃", L"饮", L"㯭", L"橹", L"㬉", L"暖", L"㪟", L"敦", L"㪚", L"散", L"㩦", L"携", L"㩗", L"携", L"㨿", L"据", L"㨪", L"晃", L"㨘", L"擤", L"㨗", L"捷", L"㧱", L"拿", L"㥫", L"惇", L"㥦", L"愜", L"㤙", L"恩", L"㢘", L"廉", L"㡓", L"裈", L"㡌", L"帽", L"㠶", L"帆", L"㠄", L"嶍", L"㠀", L"岛", L"㟁", L"岸", L"㝷", L"寻", L"㝡", L"最", L"㝠", L"冥", L"㝛", L"宿", L"㝃", L"娩", L"㚁", L"翘", L"㙲", L"壅", L"㘭", L"坳", L"㕥", L"以", L"㕠", L"双", L"㕘", L"参", L"㕑", L"厨", L"㕁", L"却", L"㓂", L"寇", L"㒺", L"罔", L"㑺", L"俊", L"拚", L"拼", L"妳", L"你", L"欅", L"榉", L"歳", L"岁", L"撃", L"击", L"龑", L"䶮", L"峯岸南", L"峯岸南", L"米泽瑠美", L"米泽瑠美", L"米澤瑠美", L"米泽瑠美", L"覆核", L"复核"};


static bool conv_gb2big(CStringW* chin)
{
    SVP_LogMsg5(L"  countof(gb2big2) %d 10447" ,  countof(gb2big2)/2 );
    for(int i = 0; i < countof(gb2big2) ; i+=2)
    {
        chin->Replace(gb2big2[i], gb2big2[i+1] );
    }
    return chin;
}
static bool conv_big2gb(CStringW* chin)
{
    SVP_LogMsg5(L"  countof(big2gb2) %d 6815" ,  countof(big2gb2)/2 );
    for(int i = 0; i < countof(big2gb2) ; i+=2)
    {
        chin->Replace(big2gb2[i], big2gb2[i+1] );
    }
    return chin;
}
void CRenderedTextSubtitle::ParseString(CSubtitle* sub, CStringW str, STSStyle& style)
{
	if(!sub) return;

	str.Replace(L"\\N", L"\n");
	str.Replace(L"\\n", (sub->m_wrapStyle < 2 || sub->m_wrapStyle == 3) ? L" " : L"\n");
	str.Replace(L"\\h", L"\x00A0");
    
    AppSettings& s = AfxGetAppSettings();
	
	//CAtlList<CString> szaEachLines;
	//Explode(str, szaEachLines, _T("\n"));
	double orgFontSize = style.fontSize * s.dGSubFontRatio;
	BOOL bNeedChkEngLine = true;
	BOOL bIsEngLine = true;

	int c_maxwidth = 49;//80000 / orgFontSize;
	//SVP_LogMsg5(L"w %d %f" , m_size.cx , orgFontSize);
	int c_curwidth = 0;
    if(s.iLanguage == 2){
        conv_gb2big(&str);
    }else if(s.iLanguage == 0){
        conv_big2gb(&str);
    }

	for(int i = 0, j = 0, len = str.GetLength(); j <= len; j++)
	{
		WCHAR c = str[j];

		if(bNeedChkEngLine){
			//检测当前行是否英文行
			bIsEngLine = true;
			for(int s1 = j; s1 <= len; s1++){
				WCHAR c1 = str[s1];
				if(c1 == '\n'){
					break;
				}
				if( !CSVPToolBox::isAlaphbet(c1) ){
					bIsEngLine = false;
					break;
				}
			}
			bNeedChkEngLine = false;
		}
		if(bIsEngLine){
			style.fontSize = orgFontSize* style.engRatio;
		}else{
			style.fontSize = orgFontSize;
		}

// 		if(c == '\n') {
// 			str.SetAt(j, ' '); continue;;
// 		}
		if( !bIsEngLine && c_curwidth > c_maxwidth ){
			str = str.Left(j) + _T("\n") + str.Right(len - j);
			len = str.GetLength();
			c = '\n';
			//SVP_LogMsg5(str);
		}else if(c != '\n' && c != ' ' && c != '\x00A0' && c != 0 ){
			if(CSVPToolBox::isAlaphbet(c) ){
				c_curwidth+=1;
			}else{
				c_curwidth+=2;
			}
			continue;
		}
		c_curwidth = 0;

		if(i < j)
		{
			if(CWord* w = new CText(style, str.Mid(i, j-i), m_ktype, m_kstart, m_kend))
			{
				//SVP_LogMsg5(L"w %d", j-i);
				sub->m_words.AddTail(w); 
				m_kstart = m_kend;
			}
		}

		if(c == '\n')
		{
			if(CWord* w = new CText(style, CStringW(), m_ktype, m_kstart, m_kend))
			{
				sub->m_words.AddTail(w); 
				m_kstart = m_kend;
			}
			bNeedChkEngLine = true; 
		}
		else if(c == ' ' || c == '\x00A0')
		{
			if(CWord* w = new CText(style, CStringW(c), m_ktype, m_kstart, m_kend))
			{
				sub->m_words.AddTail(w); 
				m_kstart = m_kend;
			}
		}

		i = j+1;
	}

	return;
}

void CRenderedTextSubtitle::ParsePolygon(CSubtitle* sub, CStringW str, STSStyle& style)
{
	if(!sub || !str.GetLength() || !m_nPolygon) return;

	if(CWord* w = new CPolygon(style, str, m_ktype, m_kstart, m_kend, sub->m_scalex/(1<<(m_nPolygon-1)), sub->m_scaley/(1<<(m_nPolygon-1)), m_polygonBaselineOffset))
	{
		sub->m_words.AddTail(w); 
		m_kstart = m_kend;
	}
}

bool CRenderedTextSubtitle::ParseSSATag(CSubtitle* sub, CStringW str, STSStyle& style, STSStyle& org, bool fAnimate)
{
	if(!sub) return(false);

	int nTags = 0, nUnrecognizedTags = 0;

	for(int i = 0, j; (j = str.Find('\\', i)) >= 0; i = j)
	{
		CStringW cmd;
		for(WCHAR c = str[++j]; c && c != '(' && c != '\\'; cmd += c, c = str[++j]);
		cmd.Trim();
		if(cmd.IsEmpty()) continue;

		CAtlArray<CStringW> params;

		if(str[j] == '(')
		{
			CStringW param;
			for(WCHAR c = str[++j]; c && c != ')'; param += c, c = str[++j]);
			param.Trim();

			while(!param.IsEmpty())
			{
				int i = param.Find(','), j = param.Find('\\');

				if(i >= 0 && (j < 0 || i < j))
				{
					CStringW s = param.Left(i).Trim();
					if(!s.IsEmpty()) params.Add(s);
					param = i+1 < param.GetLength() ? param.Mid(i+1) : L"";
				}
				else
				{
					param.Trim();
					if(!param.IsEmpty()) params.Add(param);
					param.Empty();
				}
			}
		}

		if(!cmd.Find(L"1c") || !cmd.Find(L"2c") || !cmd.Find(L"3c") || !cmd.Find(L"4c"))
			params.Add(cmd.Mid(2).Trim(L"&H")), cmd = cmd.Left(2);
		else if(!cmd.Find(L"1a") || !cmd.Find(L"2a") || !cmd.Find(L"3a") || !cmd.Find(L"4a"))
			params.Add(cmd.Mid(2).Trim(L"&H")), cmd = cmd.Left(2);
		else if(!cmd.Find(L"alpha"))
			params.Add(cmd.Mid(5).Trim(L"&H")), cmd = cmd.Left(5);
		else if(!cmd.Find(L"an"))
			params.Add(cmd.Mid(2)), cmd = cmd.Left(2);
		else if(!cmd.Find(L"a"))
			params.Add(cmd.Mid(1)), cmd = cmd.Left(1);
		else if(!cmd.Find(L"blur"))
			params.Add(cmd.Mid(4)), cmd = cmd.Left(4);
		else if(!cmd.Find(L"bord"))
			params.Add(cmd.Mid(4)), cmd = cmd.Left(4);
		else if(!cmd.Find(L"be"))
			params.Add(cmd.Mid(2)), cmd = cmd.Left(2);
		else if(!cmd.Find(L"b"))
			params.Add(cmd.Mid(1)), cmd = cmd.Left(1);
		else if(!cmd.Find(L"clip"))
			;
		else if(!cmd.Find(L"c"))
			params.Add(cmd.Mid(1).Trim(L"&H")), cmd = cmd.Left(1);
        else if(!cmd.Find(L"fade"))
			;
		else if(!cmd.Find(L"fe"))
			params.Add(cmd.Mid(2)), cmd = cmd.Left(2);
		else if(!cmd.Find(L"fn"))
			params.Add(cmd.Mid(2)), cmd = cmd.Left(2);
		else if(!cmd.Find(L"frx") || !cmd.Find(L"fry") || !cmd.Find(L"frz"))
			params.Add(cmd.Mid(3)), cmd = cmd.Left(3);
		else if(!cmd.Find(L"fax") || !cmd.Find(L"fay"))
			params.Add(cmd.Mid(3)), cmd = cmd.Left(3);
		else if(!cmd.Find(L"fr"))
			params.Add(cmd.Mid(2)), cmd = cmd.Left(2);
		else if(!cmd.Find(L"fscx") || !cmd.Find(L"fscy"))
			params.Add(cmd.Mid(4)), cmd = cmd.Left(4);
		else if(!cmd.Find(L"fsc"))
			params.Add(cmd.Mid(3)), cmd = cmd.Left(3);
		else if(!cmd.Find(L"fsp"))
			params.Add(cmd.Mid(3)), cmd = cmd.Left(3);
		else if(!cmd.Find(L"fs"))
			params.Add(cmd.Mid(2)), cmd = cmd.Left(2);
		else if(!cmd.Find(L"iclip"))
			;
		else if(!cmd.Find(L"i"))
			params.Add(cmd.Mid(1)), cmd = cmd.Left(1);
		else if(!cmd.Find(L"kt") || !cmd.Find(L"kf") || !cmd.Find(L"ko"))
			params.Add(cmd.Mid(2)), cmd = cmd.Left(2);
		else if(!cmd.Find(L"k") || !cmd.Find(L"K"))
			params.Add(cmd.Mid(1)), cmd = cmd.Left(1);
		else if(!cmd.Find(L"move"))
			;
		else if(!cmd.Find(L"org"))
			;
		else if(!cmd.Find(L"pbo"))
			params.Add(cmd.Mid(3)), cmd = cmd.Left(3);
		else if(!cmd.Find(L"pos"))
			;
		else if(!cmd.Find(L"p"))
			params.Add(cmd.Mid(1)), cmd = cmd.Left(1);
		else if(!cmd.Find(L"q"))
			params.Add(cmd.Mid(1)), cmd = cmd.Left(1);
		else if(!cmd.Find(L"r"))
			params.Add(cmd.Mid(1)), cmd = cmd.Left(1);
		else if(!cmd.Find(L"shad"))
			params.Add(cmd.Mid(4)), cmd = cmd.Left(4);
		else if(!cmd.Find(L"s"))
			params.Add(cmd.Mid(1)), cmd = cmd.Left(1);
		else if(!cmd.Find(L"t"))
			;
		else if(!cmd.Find(L"u"))
			params.Add(cmd.Mid(1)), cmd = cmd.Left(1);
		else if(!cmd.Find(L"xbord"))
			params.Add(cmd.Mid(5)), cmd = cmd.Left(5);
		else if(!cmd.Find(L"xshad"))
			params.Add(cmd.Mid(5)), cmd = cmd.Left(5);
		else if(!cmd.Find(L"ybord"))
			params.Add(cmd.Mid(5)), cmd = cmd.Left(5);
		else if(!cmd.Find(L"yshad"))
			params.Add(cmd.Mid(5)), cmd = cmd.Left(5);
		else
			nUnrecognizedTags++;

		nTags++;

		// TODO: call ParseStyleModifier(cmd, params, ..) and move the rest there

		CStringW p = params.GetCount() > 0 ? params[0] : L"";

		if(cmd == "1c" || cmd == L"2c" || cmd == L"3c" || cmd == L"4c")
		{
			int i = cmd[0] - '1';

			DWORD c = wcstol(p, NULL, 16);
			style.colors[i] = !p.IsEmpty()
				? (((int)CalcAnimation(c&0xff, style.colors[i]&0xff, fAnimate))&0xff
				  |((int)CalcAnimation(c&0xff00, style.colors[i]&0xff00, fAnimate))&0xff00
				  |((int)CalcAnimation(c&0xff0000, style.colors[i]&0xff0000, fAnimate))&0xff0000)
				: org.colors[i];
		}
		else if(cmd == L"1a" || cmd == L"2a" || cmd == L"3a" || cmd == L"4a")
		{
			int i = cmd[0] - '1';

			style.alpha[i] = !p.IsEmpty()
				? (BYTE)CalcAnimation(wcstol(p, NULL, 16), style.alpha[i], fAnimate)
				: org.alpha[i];
		}
		else if(cmd == L"alpha")
		{
			for(int i = 0; i < 4; i++)
			{
				style.alpha[i] = !p.IsEmpty()
					? (BYTE)CalcAnimation(wcstol(p, NULL, 16), style.alpha[i], fAnimate)
					: org.alpha[i];
			}
		}
		else if(cmd == L"an")
		{
			int n = wcstol(p, NULL, 10);
			if(sub->m_scrAlignment < 0)
                sub->m_scrAlignment = (n > 0 && n < 10) ? n : org.scrAlignment;
		}
		else if(cmd == L"a")
		{
			int n = wcstol(p, NULL, 10);
			if(sub->m_scrAlignment < 0)
                sub->m_scrAlignment = (n > 0 && n < 12) ? ((((n-1)&3)+1)+((n&4)?6:0)+((n&8)?3:0)) : org.scrAlignment;
		}
		else if(cmd == L"blur")
		{
			double n = CalcAnimation(wcstod(p, NULL), style.fGaussianBlur, fAnimate);
			style.fGaussianBlur = !p.IsEmpty()
				? (n < 0 ? 0 : n)
				: org.fGaussianBlur;
		}
		else if(cmd == L"bord")
		{
			double dst = wcstod(p, NULL);
			double nx = CalcAnimation(dst, style.outlineWidthX, fAnimate);
			style.outlineWidthX = !p.IsEmpty()
				? (nx < 0 ? 0 : nx)
				: org.outlineWidthX;
			double ny = CalcAnimation(dst, style.outlineWidthY, fAnimate);
			style.outlineWidthY = !p.IsEmpty()
				? (ny < 0 ? 0 : ny)
				: org.outlineWidthY;
		}
		else if(cmd == L"be")
		{
			int n = (int)(CalcAnimation(wcstol(p, NULL, 10), style.fBlur, fAnimate)+0.5);
			style.fBlur = !p.IsEmpty()
				? n
				: org.fBlur;
		}
		else if(cmd == L"b")
		{
			int n = wcstol(p, NULL, 10);
			style.fontWeight = !p.IsEmpty()
				? (n == 0 ? FW_NORMAL : n == 1 ? FW_BOLD : n >= 100 ? n : org.fontWeight)
				: org.fontWeight;
		}
		else if(cmd == L"clip" || cmd == L"iclip")
		{
			bool invert = (cmd == L"iclip");

			if(params.GetCount() == 1 && !sub->m_pClipper)
			{
				sub->m_pClipper = new CClipper(params[0], CSize(m_size.cx>>3, m_size.cy>>3), sub->m_scalex, sub->m_scaley, invert);
			}
			else if(params.GetCount() == 2 && !sub->m_pClipper)
			{
				int scale = max(wcstol(p, NULL, 10), 1);
				sub->m_pClipper = new CClipper(params[1], CSize(m_size.cx>>3, m_size.cy>>3), sub->m_scalex/(1<<(scale-1)), sub->m_scaley/(1<<(scale-1)), invert);
			}
			else if(params.GetCount() == 4)
			{
				CRect r;

				sub->m_clipInverse = invert;

				r.SetRect(
					wcstol(params[0], NULL, 10),
					wcstol(params[1], NULL, 10),
					wcstol(params[2], NULL, 10),
					wcstol(params[3], NULL, 10));

				CPoint o(0, 0);

				if(sub->m_relativeTo == 1) // TODO: this should also apply to the other two clippings above
				{
					o.x = m_vidrect.left>>3;
					o.y = m_vidrect.top>>3;
				}

				sub->m_clip.SetRect(
					(int)CalcAnimation(sub->m_scalex*r.left + o.x, sub->m_clip.left, fAnimate),
					(int)CalcAnimation(sub->m_scaley*r.top + o.y, sub->m_clip.top, fAnimate),
					(int)CalcAnimation(sub->m_scalex*r.right + o.x, sub->m_clip.right, fAnimate),
					(int)CalcAnimation(sub->m_scaley*r.bottom + o.y, sub->m_clip.bottom, fAnimate));
			}
		}
		else if(cmd == L"c")
		{
			DWORD c = wcstol(p, NULL, 16);
			style.colors[0] = !p.IsEmpty()
				? (((int)CalcAnimation(c&0xff, style.colors[0]&0xff, fAnimate))&0xff
				  |((int)CalcAnimation(c&0xff00, style.colors[0]&0xff00, fAnimate))&0xff00
				  |((int)CalcAnimation(c&0xff0000, style.colors[0]&0xff0000, fAnimate))&0xff0000)
				: org.colors[0];
		}
        else if(cmd == L"fade" || cmd == L"fad")
		{
			if(params.GetCount() == 7 && !sub->m_effects[EF_FADE])// {\fade(a1=param[0], a2=param[1], a3=param[2], t1=t[0], t2=t[1], t3=t[2], t4=t[3])
			{
				if(Effect* e = new Effect)
				{
					for(int i = 0; i < 3; i++)
						e->param[i] = wcstol(params[i], NULL, 10);
					for(int i = 0; i < 4; i++)
						e->t[i] = wcstol(params[3+i], NULL, 10);
	                
					sub->m_effects[EF_FADE] = e;
				}
			}
			else if(params.GetCount() == 2 && !sub->m_effects[EF_FADE]) // {\fad(t1=t[1], t2=t[2])
			{
				if(Effect* e = new Effect)
				{
					e->param[0] = e->param[2] = 0xff;
					e->param[1] = 0x00;
					for(int i = 1; i < 3; i++) 
						e->t[i] = wcstol(params[i-1], NULL, 10);
					e->t[0] = e->t[3] = -1; // will be substituted with "start" and "end"

					sub->m_effects[EF_FADE] = e;
				}
			}
		}
		else if(cmd == L"fax")
		{
			style.fontShiftX = !p.IsEmpty()
				? CalcAnimation(wcstod(p, NULL), style.fontShiftX, fAnimate)
				: org.fontShiftX;
		}
		else if(cmd == L"fay")
		{
			style.fontShiftY = !p.IsEmpty()
				? CalcAnimation(wcstod(p, NULL), style.fontShiftY, fAnimate)
				: org.fontShiftY;
		}
		else if(cmd == L"fe")
		{
			int n = wcstol(p, NULL, 10);
			style.charSet = !p.IsEmpty()
				? n
				: org.charSet;
		}
		else if(cmd == L"fn")
		{
			style.fontName = (!p.IsEmpty() && p != '0')
				? CString(p).Trim()
				: org.fontName;
		}
		else if(cmd == L"frx")
		{
			style.fontAngleX = !p.IsEmpty()
				? CalcAnimation(wcstod(p, NULL), style.fontAngleX, fAnimate)
				: org.fontAngleX;
		}
		else if(cmd == L"fry")
		{
			style.fontAngleY = !p.IsEmpty()
				? CalcAnimation(wcstod(p, NULL), style.fontAngleY, fAnimate)
				: org.fontAngleY;
		}
		else if(cmd == L"frz" || cmd == L"fr")
		{
			style.fontAngleZ = !p.IsEmpty()
				? CalcAnimation(wcstod(p, NULL), style.fontAngleZ, fAnimate)
				: org.fontAngleZ;
		}
		else if(cmd == L"fscx")
		{
			double n = CalcAnimation(wcstol(p, NULL, 10), style.fontScaleX, fAnimate);
			style.fontScaleX = !p.IsEmpty()
				? ((n < 0) ? 0 : n)
				: org.fontScaleX;
		}
		else if(cmd == L"fscy")
		{
			double n = CalcAnimation(wcstol(p, NULL, 10), style.fontScaleY, fAnimate);
			style.fontScaleY = !p.IsEmpty()
				? ((n < 0) ? 0 : n)
				: org.fontScaleY;
		}
		else if(cmd == L"fsc")
		{
			style.fontScaleX = org.fontScaleX;
			style.fontScaleY = org.fontScaleY;
		}
		else if(cmd == L"fsp")
		{
			style.fontSpacing = !p.IsEmpty()
				? CalcAnimation(wcstod(p, NULL), style.fontSpacing, fAnimate)
				: org.fontSpacing;
		}
		else if(cmd == L"fs")
		{
			if(!p.IsEmpty())
			{
				if(p[0] == '-' || p[0] == '+')
				{
					double n = CalcAnimation(style.fontSize + style.fontSize*wcstol(p, NULL, 10)/10, style.fontSize, fAnimate);
					style.fontSize = (n > 0) ? n : org.fontSize;
				}
				else
				{
					double n = CalcAnimation(wcstol(p, NULL, 10), style.fontSize, fAnimate);
					style.fontSize = (n > 0) ? n : org.fontSize;
				}
			}
			else
			{
				style.fontSize = org.fontSize;
			}
		}
		else if(cmd == L"i")
		{
			int n = wcstol(p, NULL, 10);
			style.fItalic = !p.IsEmpty()
				? (n == 0 ? false : n == 1 ? true : org.fItalic)
				: org.fItalic;
		}
		else if(cmd == L"kt")
		{
			m_kstart = !p.IsEmpty() 
				? wcstol(p, NULL, 10)*10
				: 0;
			m_kend = m_kstart;
		}
		else if(cmd == L"kf" || cmd == L"K")
		{
			m_ktype = 1;
			m_kstart = m_kend;
			m_kend += !p.IsEmpty() 
				? wcstol(p, NULL, 10)*10
				: 1000;
		}
		else if(cmd == L"ko")
		{
			m_ktype = 2;
			m_kstart = m_kend;
			m_kend += !p.IsEmpty() 
				? wcstol(p, NULL, 10)*10
				: 1000;
		}
		else if(cmd == L"k")
		{
			m_ktype = 0;
			m_kstart = m_kend;
			m_kend += !p.IsEmpty() 
				? wcstol(p, NULL, 10)*10
				: 1000;
		}
		else if(cmd == L"move") // {\move(x1=param[0], y1=param[1], x2=param[2], y2=param[3][, t1=t[0], t2=t[1]])}
		{
			if((params.GetCount() == 4 || params.GetCount() == 6) && !sub->m_effects[EF_MOVE])
			{
				if(Effect* e = new Effect)
				{
					e->param[0] = (int)(sub->m_scalex*wcstod(params[0], NULL)*8);
					e->param[1] = (int)(sub->m_scaley*wcstod(params[1], NULL)*8);
					e->param[2] = (int)(sub->m_scalex*wcstod(params[2], NULL)*8);
					e->param[3] = (int)(sub->m_scaley*wcstod(params[3], NULL)*8);

					e->t[0] = e->t[1] = -1;

					if(params.GetCount() == 6)
					{
						for(int i = 0; i < 2; i++)
							e->t[i] = wcstol(params[4+i], NULL, 10);
					}

					sub->m_effects[EF_MOVE] = e;
				}
			}
		}
		else if(cmd == L"org") // {\org(x=param[0], y=param[1])}
		{
			if(params.GetCount() == 2 && !sub->m_effects[EF_ORG])
			{
				if(Effect* e = new Effect)
				{
					e->param[0] = (int)(sub->m_scalex*wcstod(params[0], NULL)*8);
					e->param[1] = (int)(sub->m_scaley*wcstod(params[1], NULL)*8);

					sub->m_effects[EF_ORG] = e;
				}
			}
		}
		else if(cmd == L"pbo")
		{
			m_polygonBaselineOffset = wcstol(p, NULL, 10);
		}
		else if(cmd == L"pos")
		{
			if(params.GetCount() == 2 && !sub->m_effects[EF_MOVE])
			{
				if(Effect* e = new Effect)
				{
					e->param[0] = e->param[2] = (int)(sub->m_scalex*wcstod(params[0], NULL)*8);
					e->param[1] = e->param[3] = (int)(sub->m_scaley*wcstod(params[1], NULL)*8);
					e->t[0] = e->t[1] = 0;

					sub->m_effects[EF_MOVE] = e;
				}
			}
		}
		else if(cmd == L"p")
		{
			int n = wcstol(p, NULL, 10);
			m_nPolygon = (n <= 0 ? 0 : n);
		}
		else if(cmd == L"q")
		{
			int n = wcstol(p, NULL, 10);
			sub->m_wrapStyle = !p.IsEmpty() && (0 <= n && n <= 3)
				? n
				: m_defaultWrapStyle;
		}
		else if(cmd == L"r")
		{
			STSStyle* val;
			style = (!p.IsEmpty() && m_styles.Lookup(CString(p), val) && val) ? *val : org;
		}
		else if(cmd == L"shad")
		{
			double dst = wcstod(p, NULL);
			double nx = CalcAnimation(dst, style.shadowDepthX, fAnimate);
			style.shadowDepthX = !p.IsEmpty()
				? (nx < 0 ? 0 : nx)
				: org.shadowDepthX;
			double ny = CalcAnimation(dst, style.shadowDepthY, fAnimate);
			style.shadowDepthY = !p.IsEmpty()
				? (ny < 0 ? 0 : ny)
				: org.shadowDepthY;
		}
		else if(cmd == L"s")
		{
			int n = wcstol(p, NULL, 10);
			style.fStrikeOut = !p.IsEmpty()
				? (n == 0 ? false : n == 1 ? true : org.fStrikeOut)
				: org.fStrikeOut;
		}
		else if(cmd == L"t") // \t([<t1>,<t2>,][<accel>,]<style modifiers>)
		{
			p.Empty();

			m_animStart = m_animEnd = 0;
			m_animAccel = 1;

			if(params.GetCount() == 1)
			{
				p = params[0];
			}
			else if(params.GetCount() == 2)
			{
				m_animAccel = wcstod(params[0], NULL);
				p = params[1];
			}
			else if(params.GetCount() == 3)
			{
				m_animStart = (int)wcstod(params[0], NULL); 
				m_animEnd = (int)wcstod(params[1], NULL);
				p = params[2];
			}
			else if(params.GetCount() == 4)
			{
				m_animStart = wcstol(params[0], NULL, 10); 
				m_animEnd = wcstol(params[1], NULL, 10);
				m_animAccel = wcstod(params[2], NULL);
				p = params[3];
			}

			ParseSSATag(sub, p, style, org, true);

			sub->m_fAnimated = true;
		}
		else if(cmd == L"u")
		{
			int n = wcstol(p, NULL, 10);
			style.fUnderline = !p.IsEmpty()
				? (n == 0 ? false : n == 1 ? true : org.fUnderline)
				: org.fUnderline;
		}
		else if(cmd == L"xbord")
		{
			double dst = wcstod(p, NULL);
			double nx = CalcAnimation(dst, style.outlineWidthX, fAnimate);
			style.outlineWidthX = !p.IsEmpty()
				? (nx < 0 ? 0 : nx)
				: org.outlineWidthX;
		}
		else if(cmd == L"xshad")
		{
			double dst = wcstod(p, NULL);
			double nx = CalcAnimation(dst, style.shadowDepthX, fAnimate);
			style.shadowDepthX = !p.IsEmpty()
				? nx
				: org.shadowDepthX;
		}
		else if(cmd == L"ybord")
		{
			double dst = wcstod(p, NULL);
			double ny = CalcAnimation(dst, style.outlineWidthY, fAnimate);
			style.outlineWidthY = !p.IsEmpty()
				? (ny < 0 ? 0 : ny)
				: org.outlineWidthY;
		}
		else if(cmd == L"yshad")
		{
			double dst = wcstod(p, NULL);
			double ny = CalcAnimation(dst, style.shadowDepthY, fAnimate);
			style.shadowDepthY = !p.IsEmpty()
				? ny
				: org.shadowDepthY;
		}
	}

//	return(nUnrecognizedTags < nTags);
	return(true); // there are ppl keeping coments inside {}, lets make them happy now
}

bool CRenderedTextSubtitle::ParseHtmlTag(CSubtitle* sub, CStringW str, STSStyle& style, STSStyle& org)
{
	if(str.Find(L"!--") == 0)
		return(true);

	bool fClosing = str[0] == '/';
	str.Trim(L" /");

	int i = str.Find(' ');
	if(i < 0) i = str.GetLength();

	CStringW tag = str.Left(i).MakeLower();
	str = str.Mid(i).Trim();

	CAtlArray<CStringW> attribs, params;
	while((i = str.Find('=')) > 0)
	{
		attribs.Add(str.Left(i).Trim().MakeLower());
		str = str.Mid(i+1);
		for(i = 0; _istspace(str[i]); i++);
		str = str.Mid(i);
		if(str[0] == '\"') {str = str.Mid(1); i = str.Find('\"');}
		else i = str.Find(' ');
		if(i < 0) i = str.GetLength();
		params.Add(str.Left(i).Trim().MakeLower());
		str = str.Mid(i+1);
	}

	if(tag == L"text")
		;
	else if(tag == L"b" || tag == L"strong")
		style.fontWeight = !fClosing ? FW_BOLD : org.fontWeight;
	else if(tag == L"i" || tag == L"em")
		style.fItalic = !fClosing ? true : org.fItalic;
	else if(tag == L"u")
		style.fUnderline = !fClosing ? true : org.fUnderline;
	else if(tag == L"s" || tag == L"strike" || tag == L"del")
		style.fStrikeOut = !fClosing ? true : org.fStrikeOut;
	else if(tag == L"font")
	{
		if(!fClosing)
		{
			for(i = 0; i < attribs.GetCount(); i++)
			{
				if(params[i].IsEmpty()) continue;

				int nColor = -1;

				if(attribs[i] == L"face")
				{
					style.fontName = params[i];
				}
				else if(attribs[i] == L"size")
				{
					if(params[i][0] == '+')
						style.fontSize += wcstol(params[i], NULL, 10);
					else if(params[i][0] == '-')
						style.fontSize -= wcstol(params[i], NULL, 10);
					else
						style.fontSize = wcstol(params[i], NULL, 10);
				}
				else if(attribs[i] == L"color")
				{
					nColor = 0;
				}
				else if(attribs[i] == L"outline-color")
				{
					nColor = 2;
				}
				else if(attribs[i] == L"outline-level")
				{
					style.outlineWidthX = style.outlineWidthY = wcstol(params[i], NULL, 10);
				}
				else if(attribs[i] == L"shadow-color")
				{
					nColor = 3;
				}
				else if(attribs[i] == L"shadow-level")
				{
					style.shadowDepthX = style.shadowDepthY = wcstol(params[i], NULL, 10);
				}

				if(nColor >= 0 && nColor < 4)
				{
					CString key = WToT(params[i]).TrimLeft('#');
					DWORD val;
					if(g_colors.Lookup(key, val))
						style.colors[nColor] = val;
					else if((style.colors[nColor] = _tcstol(key, NULL, 16)) == 0)
						style.colors[nColor] = 0x00ffffff;  // default is white
					style.colors[nColor] = ((style.colors[nColor]>>16)&0xff)|((style.colors[nColor]&0xff)<<16)|(style.colors[nColor]&0x00ff00);
				}
			}
		}
		else
		{
			style.fontName = org.fontName;
			style.fontSize = org.fontSize;
			memcpy(style.colors, org.colors, sizeof(style.colors));
		}
	}
	else if(tag == L"k" && attribs.GetCount() == 1 && attribs[0] == L"t")
	{
		m_ktype = 1;
		m_kstart = m_kend;
		m_kend += wcstol(params[0], NULL, 10);
	}
	else 
		return(false);

	return(true);
}

double CRenderedTextSubtitle::CalcAnimation(double dst, double src, bool fAnimate)
{
	int s = m_animStart ? m_animStart : 0;
	int e = m_animEnd ? m_animEnd : m_delay;

	if(fabs(dst-src) >= 0.0001 && fAnimate)
	{
		if(m_time < s) dst = src;
		else if(s <= m_time && m_time < e)
		{
			double t = pow(1.0 * (m_time - s) / (e - s), m_animAccel);
			dst = (1 - t) * src + t * dst;
		}
//		else dst = dst;
	}

	return(dst);
}

CSubtitle* CRenderedTextSubtitle::GetSubtitle(int entry)
{
	CSubtitle* sub;
	if(m_subtitleCache.Lookup(entry, sub)) 
	{
		if(sub->m_fAnimated) {delete sub; sub = NULL;}
		else return(sub);
	}

	sub = new CSubtitle();
	if(!sub) return(NULL);

	CStringW str = GetStrW(entry, true);

	STSStyle stss, orgstss;
	GetStyle(entry, stss);

	if (m_ePARCompensationType == EPCTUpscale)
	{
		if (stss.fontScaleX / stss.fontScaleY == 1.0 && m_dPARCompensation != 1.0)
		{
			if (m_dPARCompensation < 1.0)
				stss.fontScaleY /= m_dPARCompensation;
			else
				stss.fontScaleX *= m_dPARCompensation;
		}
	}
	else if (m_ePARCompensationType == EPCTDownscale)
	{
		if (stss.fontScaleX / stss.fontScaleY == 1.0 && m_dPARCompensation != 1.0)
		{
			if (m_dPARCompensation < 1.0)
				stss.fontScaleX *= m_dPARCompensation;
			else
				stss.fontScaleY /= m_dPARCompensation;
		}
	}
	else if (m_ePARCompensationType == EPCTAccurateSize)
	{
		if (stss.fontScaleX / stss.fontScaleY == 1.0 && m_dPARCompensation != 1.0)
		{
			stss.fontScaleX *= m_dPARCompensation;
		}
	}

	orgstss = stss;

	sub->m_clip.SetRect(0, 0, m_size.cx>>3, m_size.cy>>3);
	sub->m_scrAlignment = -stss.scrAlignment;
	sub->m_wrapStyle = m_defaultWrapStyle;
	sub->m_fAnimated = false;
	sub->m_relativeTo = stss.relativeTo;

	sub->m_scalex = m_dstScreenSize.cx > 0 ? 1.0 * (stss.relativeTo == 1 ? m_vidrect.Width() : m_size.cx) / (m_dstScreenSize.cx*8) : 1.0;
	sub->m_scaley = m_dstScreenSize.cy > 0 ? 1.0 * (stss.relativeTo == 1 ? m_vidrect.Height() : m_size.cy) / (m_dstScreenSize.cy*8) : 1.0;

	m_animStart = m_animEnd = 0;
	m_animAccel = 1;
	m_ktype = m_kstart = m_kend = 0;
	m_nPolygon = 0;
	m_polygonBaselineOffset = 0;

	ParseEffect(sub, GetAt(entry).effect);

	while(!str.IsEmpty())
	{
		bool fParsed = false;

		int i;

		if(str[0] == '{' && (i = str.Find(L'}')) > 0)
		{
			if(fParsed = ParseSSATag(sub, str.Mid(1, i-1), stss, orgstss))
				str = str.Mid(i+1);
		}
		else if(str[0] == '<' && (i = str.Find(L'>')) > 0)
		{
			if(fParsed = ParseHtmlTag(sub, str.Mid(1, i-1), stss, orgstss))
				str = str.Mid(i+1);
		}

		if(fParsed)
		{
			i = str.FindOneOf(L"{<");
			if(i < 0) i = str.GetLength();
			if(i == 0) continue;
		}
		else
		{
			i = str.Mid(1).FindOneOf(L"{<");
			if(i < 0) i = str.GetLength()-1;
			i++;
		}

		STSStyle tmp = stss;

		tmp.fontSize = sub->m_scaley*tmp.fontSize*64;
		tmp.fontSpacing = sub->m_scalex*tmp.fontSpacing*64;
		tmp.outlineWidthX *= (m_fScaledBAS ? sub->m_scalex : 1) * 8;
		tmp.outlineWidthY *= (m_fScaledBAS ? sub->m_scaley : 1) * 8;
		tmp.shadowDepthX *= (m_fScaledBAS ? sub->m_scalex : 1) * 8;
		tmp.shadowDepthY *= (m_fScaledBAS ? sub->m_scaley : 1) * 8;

		if(m_nPolygon)
		{
			ParsePolygon(sub, str.Left(i), tmp);
		}
		else
		{
			ParseString(sub, str.Left(i), tmp);
		}

		str = str.Mid(i);
	}

	// just a "work-around" solution... in most cases nobody will want to use \org together with moving but without rotating the subs
	if(sub->m_effects[EF_ORG] && (sub->m_effects[EF_MOVE] || sub->m_effects[EF_BANNER] || sub->m_effects[EF_SCROLL]))
		sub->m_fAnimated = true;

	sub->m_scrAlignment = abs(sub->m_scrAlignment);

	STSEntry stse = GetAt(entry);
	CRect marginRect = stse.marginRect;
	if(marginRect.left == 0) marginRect.left = orgstss.marginRect.left;
	if(marginRect.top == 0) marginRect.top = orgstss.marginRect.top;
	if(marginRect.right == 0) marginRect.right = orgstss.marginRect.right;
	if(marginRect.bottom == 0) marginRect.bottom = orgstss.marginRect.bottom;
	marginRect.left = (int)(sub->m_scalex*marginRect.left*8);
	marginRect.top = (int)(sub->m_scaley*marginRect.top*8);
	marginRect.right = (int)(sub->m_scalex*marginRect.right*8);
	marginRect.bottom = (int)(sub->m_scaley*marginRect.bottom*8);
	
	if(stss.relativeTo == 1)
	{
		marginRect.left += m_vidrect.left;
		marginRect.top += m_vidrect.top;
		marginRect.right += m_size.cx - m_vidrect.right;
		marginRect.bottom += m_size.cy - m_vidrect.bottom;
	}

	sub->CreateClippers(m_size);

	sub->MakeLines(m_size, marginRect);

	m_subtitleCache[entry] = sub;

	return(sub);
}

//

STDMETHODIMP CRenderedTextSubtitle::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
    CheckPointer(ppv, E_POINTER);
    *ppv = NULL;

    return 
		QI(IPersist)
		QI(ISubStream)
		QI(ISubPicProvider)
		__super::NonDelegatingQueryInterface(riid, ppv);
}

// ISubPicProvider

STDMETHODIMP_(POSITION) CRenderedTextSubtitle::GetStartPosition(REFERENCE_TIME rt, double fps)
{
	int iSegment = -1;
	SearchSubs((int)(rt/10000), fps, &iSegment, NULL);
	
	if(iSegment < 0) iSegment = 0;

	return(GetNext((POSITION)iSegment));
}

STDMETHODIMP_(POSITION) CRenderedTextSubtitle::GetNext(POSITION pos)
{
	int iSegment = (int)pos;

	const STSSegment* stss;
	while((stss = GetSegment(iSegment)) && stss->subs.GetCount() == 0)
		iSegment++;

	return(stss ? (POSITION)(iSegment+1) : NULL);
}

STDMETHODIMP_(REFERENCE_TIME) CRenderedTextSubtitle::GetStart(POSITION pos, double fps)
{
	return(10000i64 * TranslateSegmentStart((int)pos-1, fps));
}

STDMETHODIMP_(REFERENCE_TIME) CRenderedTextSubtitle::GetStop(POSITION pos, double fps)
{
	return(10000i64 * TranslateSegmentEnd((int)pos-1, fps));
}

STDMETHODIMP_(bool) CRenderedTextSubtitle::IsAnimated(POSITION pos)
{
	// TODO
	return(true);
}

struct LSub {int idx, layer, readorder;};

static int lscomp(const void* ls1, const void* ls2)
{
	int ret = ((LSub*)ls1)->layer - ((LSub*)ls2)->layer;
	if(!ret) ret = ((LSub*)ls1)->readorder - ((LSub*)ls2)->readorder;
	return(ret);
}

STDMETHODIMP CRenderedTextSubtitle::Render(SubPicDesc& spd, REFERENCE_TIME rt, double fps, RECT& bbox)
{
	CRect bbox2(0,0,0,0);

	if(m_size != CSize(spd.w*8, spd.h*8) || m_vidrect != CRect(spd.vidrect.left*8, spd.vidrect.top*8, spd.vidrect.right*8, spd.vidrect.bottom*8))
		Init(CSize(spd.w, spd.h), spd.vidrect);

	int t = (int)(rt / 10000);

	int segment;
	const STSSegment* stss = SearchSubs(t, fps, &segment);
	if(!stss) return S_FALSE;

	// clear any cached subs not in the range of +/-30secs measured from the segment's bounds
	{
		POSITION pos = m_subtitleCache.GetStartPosition();
		while(pos)
		{
			int key;
			CSubtitle* value;
			m_subtitleCache.GetNextAssoc(pos, key, value);

			STSEntry& stse = GetAt(key);
			if(stse.end <= (t-30000) || stse.start > (t+30000)) 
			{
				delete value;
				m_subtitleCache.RemoveKey(key);
				pos = m_subtitleCache.GetStartPosition();
			}
		}
	}

	m_sla.AdvanceToSegment(segment, stss->subs);

	CAtlArray<LSub> subs;

	for(int i = 0, j = stss->subs.GetCount(); i < j; i++)
	{
		LSub ls;
		ls.idx = stss->subs[i];
		ls.layer = GetAt(stss->subs[i]).layer;
		ls.readorder = GetAt(stss->subs[i]).readorder;
		subs.Add(ls);
	}

	qsort(subs.GetData(), subs.GetCount(), sizeof(LSub), lscomp);

	for(int i = 0, j = subs.GetCount(); i < j; i++)
	{
		int entry = subs[i].idx;

		STSEntry stse = GetAt(entry);

		{
			int start = TranslateStart(entry, fps);
			m_time = t - start;
			m_delay = TranslateEnd(entry, fps) - start;
		}

		CSubtitle* s = GetSubtitle(entry);
		if(!s) continue;

		CRect clipRect = s->m_clip;
		CRect r = s->m_rect;
		CSize spaceNeeded = r.Size();

		// apply the effects

		bool fPosOverride = false, fOrgOverride = false;

		int alpha = 0x00;

		CPoint org2;

		for(int k = 0; k < EF_NUMBEROFEFFECTS; k++)
		{
			if(!s->m_effects[k]) continue;

			switch(k)
			{
			case EF_MOVE: // {\move(x1=param[0], y1=param[1], x2=param[2], y2=param[3], t1=t[0], t2=t[1])}
				{
					CPoint p;
					CPoint p1(s->m_effects[k]->param[0], s->m_effects[k]->param[1]);
					CPoint p2(s->m_effects[k]->param[2], s->m_effects[k]->param[3]);
					int t1 = s->m_effects[k]->t[0];
					int t2 = s->m_effects[k]->t[1];

					if(t2 < t1) {int t = t1; t1 = t2; t2 = t;}

					if(t1 <= 0 && t2 <= 0) {t1 = 0; t2 = m_delay;}

					if(m_time <= t1) p = p1;
					else if (p1 == p2) p = p1;
					else if(t1 < m_time && m_time < t2)
					{
						double t = 1.0*(m_time-t1)/(t2-t1);
						p.x = (int)((1-t)*p1.x + t*p2.x);
						p.y = (int)((1-t)*p1.y + t*p2.y);
					}
					else p = p2;

					r = CRect(
							CPoint((s->m_scrAlignment%3) == 1 ? p.x : (s->m_scrAlignment%3) == 0 ? p.x - spaceNeeded.cx : p.x - (spaceNeeded.cx+1)/2,
									s->m_scrAlignment <= 3 ? p.y - spaceNeeded.cy : s->m_scrAlignment <= 6 ? p.y - (spaceNeeded.cy+1)/2 : p.y),
							spaceNeeded);

					if(s->m_relativeTo == 1)
						r.OffsetRect(m_vidrect.TopLeft());

					fPosOverride = true;
				}
				break;
			case EF_ORG: // {\org(x=param[0], y=param[1])}
				{
					org2 = CPoint(s->m_effects[k]->param[0], s->m_effects[k]->param[1]);

					fOrgOverride = true;
				}
				break;
			case EF_FADE: // {\fade(a1=param[0], a2=param[1], a3=param[2], t1=t[0], t2=t[1], t3=t[2], t4=t[3]) or {\fad(t1=t[1], t2=t[2])
				{
					int t1 = s->m_effects[k]->t[0];
					int t2 = s->m_effects[k]->t[1];
					int t3 = s->m_effects[k]->t[2];
					int t4 = s->m_effects[k]->t[3];

					if(t1 == -1 && t4 == -1) {t1 = 0; t3 = m_delay-t3; t4 = m_delay;}

					if(m_time < t1) alpha = s->m_effects[k]->param[0];
					else if(m_time >= t1 && m_time < t2)
					{
						double t = 1.0 * (m_time - t1) / (t2 - t1);
						alpha = (int)(s->m_effects[k]->param[0]*(1-t) + s->m_effects[k]->param[1]*t);
					}
					else if(m_time >= t2 && m_time < t3) alpha = s->m_effects[k]->param[1];
					else if(m_time >= t3 && m_time < t4)
					{
						double t = 1.0 * (m_time - t3) / (t4 - t3);
						alpha = (int)(s->m_effects[k]->param[1]*(1-t) + s->m_effects[k]->param[2]*t);
					}
					else if(m_time >= t4) alpha = s->m_effects[k]->param[2];
				}
				break;
			case EF_BANNER: // Banner;delay=param[0][;leftoright=param[1];fadeawaywidth=param[2]]
				{
					int left = s->m_relativeTo == 1 ? m_vidrect.left : 0, 
						right = s->m_relativeTo == 1 ? m_vidrect.right : m_size.cx;

					r.left = !!s->m_effects[k]->param[1] 
						? (left/*marginRect.left*/ - spaceNeeded.cx) + (int)(m_time*8.0/s->m_effects[k]->param[0])
						: (right /*- marginRect.right*/) - (int)(m_time*8.0/s->m_effects[k]->param[0]);

					r.right = r.left + spaceNeeded.cx;

					clipRect &= CRect(left>>3, clipRect.top, right>>3, clipRect.bottom);

					fPosOverride = true;
				}
				break;
			case EF_SCROLL: // Scroll up/down(toptobottom=param[3]);top=param[0];bottom=param[1];delay=param[2][;fadeawayheight=param[4]]
				{
					r.top = !!s->m_effects[k]->param[3]
						? s->m_effects[k]->param[0] + (int)(m_time*8.0/s->m_effects[k]->param[2]) - spaceNeeded.cy
						: s->m_effects[k]->param[1] - (int)(m_time*8.0/s->m_effects[k]->param[2]);

					r.bottom = r.top + spaceNeeded.cy;

					CRect cr(0, (s->m_effects[k]->param[0] + 4) >> 3, spd.w, (s->m_effects[k]->param[1] + 4) >> 3);

					if(s->m_relativeTo == 1)
						r.top += m_vidrect.top, 
						r.bottom += m_vidrect.top, 
						cr.top += m_vidrect.top>>3, 
						cr.bottom += m_vidrect.top>>3;

					clipRect &= cr;

					fPosOverride = true;
				}
				break;
			default:
				break;
			}
		}

		if(!fPosOverride && !fOrgOverride && !s->m_fAnimated) 
			r = m_sla.AllocRect(s, segment, entry, stse.layer, m_collisions);

		CPoint org;
		org.x = (s->m_scrAlignment%3) == 1 ? r.left : (s->m_scrAlignment%3) == 2 ? r.CenterPoint().x : r.right;
		org.y = s->m_scrAlignment <= 3 ? r.bottom : s->m_scrAlignment <= 6 ? r.CenterPoint().y : r.top;

		if(!fOrgOverride) org2 = org;

		BYTE* pAlphaMask = s->m_pClipper?s->m_pClipper->m_pAlphaMask:NULL;

		CPoint p, p2(0, r.top);

		POSITION pos;

		p = p2;

		// Rectangles for inverse clip
		CRect iclipRect[4];
		iclipRect[0] = CRect(0, 0, spd.w, clipRect.top);
		iclipRect[1] = CRect(0, clipRect.top, clipRect.left, clipRect.bottom);
		iclipRect[2] = CRect(clipRect.right, clipRect.top, spd.w, clipRect.bottom);
		iclipRect[3] = CRect(0, clipRect.bottom, spd.w, spd.h);

		pos = s->GetHeadPosition();
		while(pos) 
		{
			CLine* l = s->GetNext(pos);

			p.x = (s->m_scrAlignment%3) == 1 ? org.x
				: (s->m_scrAlignment%3) == 0 ? org.x - l->m_width
				:							   org.x - (l->m_width/2);

			if (s->m_clipInverse)
			{
				bbox2 |= l->PaintShadow(spd, iclipRect[0], pAlphaMask, p, org2, m_time, alpha);
				bbox2 |= l->PaintShadow(spd, iclipRect[1], pAlphaMask, p, org2, m_time, alpha);
				bbox2 |= l->PaintShadow(spd, iclipRect[2], pAlphaMask, p, org2, m_time, alpha);
				bbox2 |= l->PaintShadow(spd, iclipRect[3], pAlphaMask, p, org2, m_time, alpha);
			}
			else
			{
				bbox2 |= l->PaintShadow(spd, clipRect, pAlphaMask, p, org2, m_time, alpha);
			}

			p.y += l->m_ascent + l->m_descent;
		}

		p = p2;

		pos = s->GetHeadPosition();
		while(pos) 
		{
			CLine* l = s->GetNext(pos);

			p.x = (s->m_scrAlignment%3) == 1 ? org.x
				: (s->m_scrAlignment%3) == 0 ? org.x - l->m_width
				:							   org.x - (l->m_width/2);

			if (s->m_clipInverse)
			{
				bbox2 |= l->PaintOutline(spd, iclipRect[0], pAlphaMask, p, org2, m_time, alpha);
				bbox2 |= l->PaintOutline(spd, iclipRect[1], pAlphaMask, p, org2, m_time, alpha);
				bbox2 |= l->PaintOutline(spd, iclipRect[2], pAlphaMask, p, org2, m_time, alpha);
				bbox2 |= l->PaintOutline(spd, iclipRect[3], pAlphaMask, p, org2, m_time, alpha);
			}
			else
			{
				bbox2 |= l->PaintOutline(spd, clipRect, pAlphaMask, p, org2, m_time, alpha);
			}

			p.y += l->m_ascent + l->m_descent;
		}

		p = p2;

		pos = s->GetHeadPosition();
		while(pos) 
		{
			CLine* l = s->GetNext(pos);

			p.x = (s->m_scrAlignment%3) == 1 ? org.x
				: (s->m_scrAlignment%3) == 0 ? org.x - l->m_width
				:							   org.x - (l->m_width/2);

			if (s->m_clipInverse)
			{
				bbox2 |= l->PaintBody(spd, iclipRect[0], pAlphaMask, p, org2, m_time, alpha);
				bbox2 |= l->PaintBody(spd, iclipRect[1], pAlphaMask, p, org2, m_time, alpha);
				bbox2 |= l->PaintBody(spd, iclipRect[2], pAlphaMask, p, org2, m_time, alpha);
				bbox2 |= l->PaintBody(spd, iclipRect[3], pAlphaMask, p, org2, m_time, alpha);
			}
			else
			{
				bbox2 |= l->PaintBody(spd, clipRect, pAlphaMask, p, org2, m_time, alpha);
			}

			p.y += l->m_ascent + l->m_descent;
		}
	}

	bbox = bbox2;

	return (subs.GetCount() && !bbox2.IsRectEmpty()) ? S_OK : S_FALSE;
}

// IPersist

STDMETHODIMP CRenderedTextSubtitle::GetClassID(CLSID* pClassID)
{
	return pClassID ? *pClassID = __uuidof(this), S_OK : E_POINTER;
}

// ISubStream

STDMETHODIMP_(int) CRenderedTextSubtitle::GetStreamCount()
{
	return(1);
}

STDMETHODIMP CRenderedTextSubtitle::GetStreamInfo(int iStream, WCHAR** ppName, LCID* pLCID)
{
	if(iStream != 0) return E_INVALIDARG;

	if(ppName)
	{
		if(!(*ppName = (WCHAR*)CoTaskMemAlloc((m_name.GetLength()+1)*sizeof(WCHAR))))
			return E_OUTOFMEMORY;

		wcscpy(*ppName, CStringW(m_name));
	}

	if(pLCID)
	{
		*pLCID = 0; // TODO
	}

	return S_OK;
}

STDMETHODIMP_(int) CRenderedTextSubtitle::GetStream()
{
	return(0);
}

STDMETHODIMP CRenderedTextSubtitle::SetStream(int iStream)
{
	return iStream == 0 ? S_OK : E_FAIL;
}

STDMETHODIMP CRenderedTextSubtitle::Reload()
{
	CFileStatus s;
	if(!CFile::GetStatus(m_path, s)) return E_FAIL;
	return !m_path.IsEmpty() && Open(m_path, DEFAULT_CHARSET) ? S_OK : E_FAIL;
}
