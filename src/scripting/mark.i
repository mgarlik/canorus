/*! 
	Copyright (c) 2007, Matevž Jekovec, Canorus development team
	All Rights Reserved. See AUTHORS for a complete list of authors.
	
	Licensed under the GNU GENERAL PUBLIC LICENSE. See COPYING for details.
*/

%{
#include "core/mark.h"
#include "core/articulation.h"
%}

%rename(Mark) CAMark;
%include "core/mark.h"
%rename(Articulation) CAArticulation;
%include "core/articulation.h"