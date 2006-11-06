/** @file drawable/drawablefunctionmarking.cpp
 * 
 * Copyright (c) 2006, Matevž Jekovec, Canorus development team
 * All Rights Reserved. See AUTHORS for a complete list of authors.
 * 
 * Licensed under the GNU GENERAL PUBLIC LICENSE. See COPYING for details.
 */

#include <QPainter>
#include <QFont>

#include "drawable/drawablefunctionmarking.h"
#include "drawable/drawablefunctionmarkingcontext.h"
#include "core/functionmarking.h"

////////////////////////////////////////////////////
// class CADrawableFunctionMarking
////////////////////////////////////////////////////
CADrawableFunctionMarking::CADrawableFunctionMarking(CAFunctionMarking *function, CADrawableFunctionMarkingContext *context, int x, int y)
 : CADrawableMusElement(function, context, x, y) {
 	_drawableMusElementType = CADrawableMusElement::DrawableFunctionMarking;
 	
 	_extenderLineVisible = false;
 	if (functionMarking()->tonicDegree()==CAFunctionMarking::None)
		switch (functionMarking()->function()) { //TODO: Width determination should be done automatically using QPainter::boundingRect() method
			//character widths are calculated using FreeSans font, pixelSize 19
			case CAFunctionMarking::I:		_text="I"; _width=5; break;
			case CAFunctionMarking::II:		_text="II"; _width=10; break;
			case CAFunctionMarking::III:	_text="III"; _width=15; break;
			case CAFunctionMarking::IV:		_text="IV"; _width=18; break;
			case CAFunctionMarking::V:		_text="V"; _width=13; break;
			case CAFunctionMarking::VI:		_text="VI"; _width=18; break;
			case CAFunctionMarking::VII:	_text="VII"; _width=23; break;
			case CAFunctionMarking::T:		_text="T"; _width=12; break;
			case CAFunctionMarking::S:		_text="S"; _width=13; break;
			case CAFunctionMarking::D:		_text="D"; _width=14; break;
			case CAFunctionMarking::F:		_text="F"; _width=12; break;
			case CAFunctionMarking::N:		_text="N"; _width=14; break;
			case CAFunctionMarking::L:		_text="L"; _width=11; break;
		}
	else
		switch (functionMarking()->function()) { //TODO: Width determination should be done automatically using QPainter::boundingRect() method
			//character widths are calculated using FreeSans font, pixelSize 17
			case CAFunctionMarking::I:		_text="I"; _width=5; break;
			case CAFunctionMarking::II:		_text="II"; _width=10; break;
			case CAFunctionMarking::III:	_text="III"; _width=15; break;
			case CAFunctionMarking::IV:		_text="IV"; _width=16; break;
			case CAFunctionMarking::V:		_text="V"; _width=11; break;
			case CAFunctionMarking::VI:		_text="VI"; _width=16; break;
			case CAFunctionMarking::VII:	_text="VII"; _width=21; break;
			case CAFunctionMarking::T:		_text="T"; _width=10; break;
			case CAFunctionMarking::S:		_text="S"; _width=11; break;
			case CAFunctionMarking::D:		_text="D"; _width=12; break;
			case CAFunctionMarking::F:		_text="F"; _width=10; break;
			case CAFunctionMarking::N:		_text="N"; _width=12; break;
			case CAFunctionMarking::L:		_text="L"; _width=9; break;
		}	
	
 	_fontWidth = 10;
	if (function->isMinor()) { //prepend a small circle
		_text.prepend(QString(0x02DA));
		_width+=6;
		_fontWidth+=6;
		_xPos -= 6;
	}
	
 	_height=15;
 	_neededWidth = _width;
 	_neededHeight = _height;
}

CADrawableFunctionMarking::~CADrawableFunctionMarking() {
	
}

void CADrawableFunctionMarking::draw(QPainter *p, CADrawSettings s) {
	QFont font("FreeSans");
	font.setPixelSize((int)(19*s.z)); //use pixelSize instead of size as we want fixed font size no matter on screen DPI
	p->setPen(QPen(s.color));
	p->setFont(font);
	p->drawText(s.x, s.y+(int)(_height*s.z+0.5), _text);
	
	if (_extenderLineVisible)
		p->drawLine(s.x + (int)(_fontWidth*s.z+0.5), s.y+(int)((_height/2.0)*s.z+0.5),
		            s.x + (int)(_width*s.z+0.5), s.y+(int)((_height/2.0)*s.z+0.5));
}

CADrawableFunctionMarking *CADrawableFunctionMarking::clone() {
	return new CADrawableFunctionMarking(functionMarking(), drawableFunctionMarkingContext(), ((CAFunctionMarking*)_musElement)->isMinor()?xPos()+6:xPos(), yPos());
}

////////////////////////////////////////////////////
// class CADrawableFunctionMarkingSupport
////////////////////////////////////////////////////
CADrawableFunctionMarkingSupport::CADrawableFunctionMarkingSupport(CADrawableFunctionMarkingSupportType type, const QString key, CADrawableContext *c, int x, int y)
 : CADrawableMusElement(0, c, x, y) {	//support functions point to no music element
	_drawableMusElementType = CADrawableMusElement::DrawableFunctionMarkingSupport;
	_drawableFunctionMarkingSupportType = type;
	_key = key;
	
	if (type==Key) {
		_width = 0;	//TODO: Width determination should be done automatically using QPainter::boundingRect() method
		for (int i=0; i<key.size(); i++) {	//character widths are calculated using FreeSans font, pixelSize 17
			if (key[i]=='C') _width+=12;
			else if (key[i]=='D') _width+=12;
			else if (key[i]=='E') _width+=11;
			else if (key[i]=='F') _width+=10;
			else if (key[i]=='G') _width+=13;
			else if (key[i]=='A') _width+=11;
			else if (key[i]=='B') _width+=11;
			else if (key[i]=='c') _width+=9;
			else if (key[i]=='d') _width+=9;
			else if (key[i]=='e') _width+=9;
			else if (key[i]=='f') _width+=5;
			else if (key[i]=='g') _width+=9;
			else if (key[i]=='a') _width+=9;
			else if (key[i]=='b') _width+=9;
			else if (key[i]=='i') _width+=5;
			else if (key[i]=='s') _width+=9;
		}
		_width += 5;	//colon after the key name (:)
		_height = 14;
	}
	
	_neededWidth = _width;
	_neededHeight = _height;
	_selectable = false;
}

CADrawableFunctionMarkingSupport::CADrawableFunctionMarkingSupport(CADrawableFunctionMarkingSupportType type, CADrawableFunctionMarking *f1, CADrawableContext *c, int x, int y, CADrawableFunctionMarking *f2)
 : CADrawableMusElement(0, c, x, y) {	//support functions point to no music element
	_drawableMusElementType = CADrawableMusElement::DrawableFunctionMarkingSupport;
	_drawableFunctionMarkingSupportType = type;
	_function1 = f1;
	_function2 = f2;
	_width=0;
	_extenderLineVisible = false;
	
	if (f1->functionMarking()->isTonicDegreeMinor()) {
		_width+=6;
	}
	if (type==ChordArea) {
		//character widths are calculated using FreeSans font, pixelSize 17
		//TODO: Width determination should be done automatically using QPainter::boundingRect() method
		if (f1->functionMarking()->chordArea()==CAFunctionMarking::T) { _width=10; }
		else if (f1->functionMarking()->chordArea()==CAFunctionMarking::S) { _width=11; }
		else if (f1->functionMarking()->chordArea()==CAFunctionMarking::D) { _width=12; }
		
		_width += 12;	//paranthesis
		_height = 14;
	} else if (type==Tonicization) {
		//character widths are calculated using FreeSans font, pixelSize 19
		//TODO: Width determination should be done automatically using QPainter::boundingRect() method
		switch (f1->functionMarking()->tonicDegree()) {
			case CAFunctionMarking::I:		_width+=5; break;
			case CAFunctionMarking::II:		_width+=10; break;
			case CAFunctionMarking::III:	_width+=15; break;
			case CAFunctionMarking::IV:		_width+=16; break;
			case CAFunctionMarking::V:		_width+=11; break;
			case CAFunctionMarking::VI:		_width+=16; break;
			case CAFunctionMarking::VII:	_width+=21; break;
			case CAFunctionMarking::T:		_width+=10; break;
			case CAFunctionMarking::S:		_width+=11; break;
			case CAFunctionMarking::D:		_width+=12; break;
		}
		if (!f2) {
			_width = (_width>f1->width()?_width:f1->width());	//single tonicization
		} else {
			_width = f2->xPos() + f2->width() - f1->xPos();		//multiple tonicization
		}
		_height = 15;
	} else if (type==Rectangle) {
		_width = f2->xPos()+f2->width() - f1->xPos() + 6;
		_xPos -= 3;
		_height = f2->yPos()+f2->height() - f1->yPos() + 6;
		_yPos -= 3;
	} else if (type==Ellipse) {
		_width = (int)((f2->xPos()+f2->width()/2.0) - (f1->xPos()+f1->width()/2.0) + 0.5);
		_xPos += (int)(f1->width()/2.0+0.5);
		_height = 14;
	}
	
	_neededWidth = _width;
	_neededHeight = _height;
	_selectable = false;
}

CADrawableFunctionMarkingSupport::~CADrawableFunctionMarkingSupport() {
}

void CADrawableFunctionMarkingSupport::draw(QPainter *p, const CADrawSettings s) {
	QFont font("FreeSans");
	QString text;
	CAFunctionMarking::CAFunctionType type;
	bool minor;
	
	//prepare drawing stuff
	switch (_drawableFunctionMarkingSupportType) {
		case Key:
			font.setPixelSize((int)(17*s.z+0.5));
			break;
		case ChordArea:
			font.setPixelSize((int)(17*s.z+0.5));
			type = _function1->functionMarking()->chordArea();
			minor=_function1->functionMarking()->isChordAreaMinor();
			break;
		case Tonicization:
			font.setPixelSize((int)(19*s.z+0.5));
			type = _function1->functionMarking()->tonicDegree();
			minor=_function1->functionMarking()->isTonicDegreeMinor();
			break;
		case Ellipse:
			font.setPixelSize((int)(14*s.z+0.5));
			break;
	}
	
	//fill in the text values for functions
 	if (_drawableFunctionMarkingSupportType==Tonicization || _drawableFunctionMarkingSupportType==ChordArea) {
		switch (type) { //TODO: Width determination should be done automatically using QPainter::boundingRect() method
			//character widths are calculated using FreeSans font, pixelSize 19
			case CAFunctionMarking::I:		text="I"; break;
			case CAFunctionMarking::II:		text="II"; break;
			case CAFunctionMarking::III:	text="III"; break;
			case CAFunctionMarking::IV:		text="IV"; break;
			case CAFunctionMarking::V:		text="V"; break;
			case CAFunctionMarking::VI:		text="VI"; break;
			case CAFunctionMarking::VII:	text="VII"; break;
			case CAFunctionMarking::T:		text="T"; break;
			case CAFunctionMarking::S:		text="S"; break;
			case CAFunctionMarking::D:		text="D"; break;
			case CAFunctionMarking::F:		text="F"; break;
			case CAFunctionMarking::N:		text="N"; break;
			case CAFunctionMarking::L:		text="L"; break;
		}
		
		if (minor)
			text.prepend(QString(0x02DA));
 	}
 	
	p->setPen(QPen(s.color));
	p->setFont(font);
	
	//draw
	switch (_drawableFunctionMarkingSupportType) {
		case Key:
			p->drawText(s.x, s.y+(int)(_height*s.z+0.5), _key+":");
			break;
		case ChordArea:
			p->drawText(s.x, s.y+(int)(_height*s.z+0.5), QString("(")+text+")");
			break;
		case Tonicization:
			if (!_function2) {	//tonicization is below a single function
				p->drawText(s.x, s.y+(int)(_height*s.z+0.5), text);
				if (_extenderLineVisible)
					p->drawLine(s.x + p->boundingRect(0,0,0,0,0,"text").width(), (int)(s.y+height()/2.0+0.5),
								(int)(s.x + width()*s.z+0.5), (int)(s.y+height()/2.0+0.5));
			} else {
				p->drawText(	//tonicization is below multiple functions
					//get the x-coordinate where to start text rendering
					(int)(s.x + (_function2->xPos() + _function2->width() - _function1->xPos())*s.z/2.0 - p->boundingRect(0,0,0,0,0,text).width()/2.0 + 0.5),
					s.y+(int)(_height*s.z+0.5),
					text
				);
				p->drawLine(s.x, s.y, s.x + (int)(width()*s.z+0.5), s.y);
				
				if (_extenderLineVisible)
					p->drawLine(s.x + p->boundingRect(0,0,0,0,0,"text").width(), (int)(s.y+height()/2.0+0.5),
								(int)(s.x + width()*s.z+0.5), (int)(s.y+height()/2.0+0.5));
				
			}
			break;
		case Ellipse:
			//draw vertical lines
			p->drawLine(s.x, s.y, s.x, (int)(s.y+height()*s.z/2.0+0.5));
			p->drawLine((int)(s.x+width()*s.z/2.0+0.5), s.y, (int)(s.x+width()*s.z/2.0+0.5), (int)(s.y+height()*s.z/2.0+0.5));
			p->drawLine(s.x, (int)(s.y+height()*s.z/2.0+0.5), (int)(s.x+width()*s.z/2.0 - 4 + 0.5), (int)(s.y+height()*s.z/2.0+0.5));
			p->drawLine((int)(s.x+width()*s.z/2.0 + 4 + 0.5), (int)(s.y+height()*s.z/2.0+0.5), (int)(s.x+width()*s.z+0.5), (int)(s.y+height()*s.z/2.0+0.5));
			
			p->drawText((int)(s.x+width()*s.z/2.0 - 2 + 0.5), (int)(s.y+3*s.z+0.5), "E");
			
			break;
			
		case Rectangle:
			p->drawRect(s.x, s.y, (int)(s.x+width()*s.z+0.5), (int)(s.y+height()*s.z+0.5));
			break;
	}
}

CADrawableFunctionMarkingSupport *CADrawableFunctionMarkingSupport::clone() {
	switch (_drawableFunctionMarkingSupportType) {
		case Key:
			return new CADrawableFunctionMarkingSupport(Key, _key, _drawableContext, _xPos, _yPos);
			break;
		case ChordArea:
		case Tonicization:
			return new CADrawableFunctionMarkingSupport(_drawableFunctionMarkingSupportType, _function1, _drawableContext, _xPos, _yPos, _function2);
			break;
		case Ellipse:
			return new CADrawableFunctionMarkingSupport(Ellipse, _function1, _drawableContext, (int)(_xPos - _function1->width()/2.0+0.5), _yPos, _function2);
		case Rectangle:
			return new CADrawableFunctionMarkingSupport(Rectangle, _function1, _drawableContext, _xPos+3, _yPos+3, _function2);
	}
}