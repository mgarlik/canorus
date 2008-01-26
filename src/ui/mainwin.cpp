/*! 
	Copyright (c) 2006-2007, Reinhard Katzmann, Matevž Jekovec, Canorus development team
	All Rights Reserved. See AUTHORS for a complete list of authors.
	
	Licensed under the GNU GENERAL PUBLIC LICENSE. See COPYING for details.
*/

//#include <Python.h> must be called before standard headers inclusion. See http://docs.python.org/api/includes.html
#ifdef USE_PYTHON
#include <Python.h>
#endif

#include <QtGui>
#include <QSlider>
#include <QComboBox>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPoint>
#include <QKeyEvent>
#include <QString>
#include <QTextStream>
#include <QXmlInputSource>
#include <QComboBox>
#include <QCheckBox>
#include <QThread>
#include <iostream>

#include "ui/mainwin.h"
#include "ui/settingsdialog.h"
#include "ui/propertiesdialog.h"

#include "interface/playback.h"
#include "interface/engraver.h"
#include "interface/pluginmanager.h"
#include "interface/mididevice.h"
#include "interface/rtmididevice.h"

#include "widgets/menutoolbutton.h"
#include "widgets/undotoolbutton.h"
#include "widgets/lcdnumber.h"

#include "widgets/viewport.h"
#include "widgets/viewportcontainer.h"
#include "widgets/scoreviewport.h"
#include "widgets/sourceviewport.h"

#include "drawable/drawablecontext.h"
#include "drawable/drawablestaff.h"
#include "drawable/drawablelyricscontext.h"
#include "drawable/drawablemuselement.h"
#include "drawable/drawablenote.h"

#include "canorus.h"
#include "core/settings.h"
#include "core/sheet.h"
#include "core/staff.h"
#include "core/functionmarkingcontext.h"
#include "core/lyricscontext.h"
#include "core/clef.h"
#include "core/articulation.h"
#include "core/keysignature.h"
#include "core/note.h"
#include "core/slur.h"
#include "core/rest.h"
#include "core/voice.h"
#include "core/barline.h"
#include "core/timesignature.h"
#include "core/syllable.h"
#include "core/functionmarking.h"
#include "core/dynamic.h"
#include "core/instrumentchange.h"
#include "core/repeatmark.h"
#include "core/text.h"
#include "core/ritardando.h"
#include "core/bookmark.h"
#include "core/fingering.h"
#include "core/muselementfactory.h"
#include "core/mimedata.h"
#include "core/undo.h"

#include "scripting/swigruby.h"
#include "scripting/swigpython.h"

#include "export/lilypondexport.h"
#include "export/canorusmlexport.h"
#include "export/canexport.h"
#include "export/midiexport.h"
#include "import/lilypondimport.h"
#include "import/canorusmlimport.h"
#include "import/canimport.h"

/*!
	\class CAMainWin
	\brief Canorus main window
	Class CAMainWin represents Canorus main window.
	The core layout is generated using the Qt designer's ui/mainwin.ui file.
	Other widgets (specific toolbars, viewports, plugin menus) are generated manually in-code.
	
	Canorus supports multiple main windows pointing to the same document or separated document.
	
	Canorus uses multiple inheritance approach. See
	http://doc.trolltech.com/4.2/designer-using-a-component.html#the-multiple-inheritance-approach
	Class members having _ prefix are general private properties.
	Private attributes with ui prefix are GUI-only widgets objects created in Qt designer or manually. 
	
	\sa CAViewPort, CACanorus
*/

QFileDialog *CAMainWin::uiOpenDialog = 0;
QFileDialog *CAMainWin::uiSaveDialog = 0;
QFileDialog *CAMainWin::uiImportDialog = 0;
QFileDialog *CAMainWin::uiExportDialog = 0;

/*!
	Default constructor.
	Creates Canorus main window with parent \a oParent. Parent is usually null.
*/
CAMainWin::CAMainWin(QMainWindow *oParent)
 : QMainWindow( oParent ) {
	setAttribute( Qt::WA_DeleteOnClose );
	
	// Locate resources (images, icons)
	QString currentPath = QDir::currentPath();
	
	QList<QString> resourcesLocations = CACanorus::locateResourceDir(QString("images"));
	if (!resourcesLocations.size()) // when Canorus not installed, search the source path
		resourcesLocations = CACanorus::locateResourceDir(QString("ui/images"));
		
	QDir::setCurrent( resourcesLocations[0] ); /// \todo Button and menu icons by default look at the current working directory as their resource path only. QResource::addSearchPath() doesn't work for external icons. Any other ideas? -Matevz
	// Create the GUI (actions, toolbars, menus etc.)
	createCustomActions();
	setupUi( this ); // initialize elements created by Qt Designer
	setupCustomUi();
	QDir::setCurrent( currentPath );
	
	// Explicitly initialize this so it isn't true sometimes
	setRebuildUILock( false );
	
	// Initialize internal UI properties
	_mode = SelectMode;
	_playback = 0;
	_animatedScroll = true;
	_lockScrollPlayback = false;
	
	// Create plugins menus and toolbars in this main window
	CAPluginManager::enablePlugins(this);
	
	// Connects MIDI IN callback function to a local slot. Not implemented yet.
	connect( CACanorus::midiDevice(), SIGNAL(midiInEvent( QVector<unsigned char> )), this, SLOT(onMidiInEvent( QVector<unsigned char> )) );
	
	// Connect QTimer so it increases the local document edited time every second
	restartTimeEditedTime();
	connect( &_timeEditedTimer, SIGNAL(timeout()), this, SLOT(onTimeEditedTimerTimeout()) );
	_timeEditedTimer.start(1000);
	
	setDocument( 0 );
	CACanorus::addMainWin( this );
}

CAMainWin::~CAMainWin()  {
	delete _musElementFactory;

	if (CACanorus::mainWinCount(document()) == 1) { 
		CACanorus::undo()->deleteUndoStack(document()); // delete undo stack when the last document deleted
		delete document();
	}
	
	CACanorus::removeMainWin(this); // must be called *after* CACanorus::deleteUndoStack()
	if(_playback)
		delete _playback;

	// clear UI
	delete uiInsertToolBar; // also deletes content of the toolbars
	delete uiInsertGroup;
	
	delete uiVoiceToolBar;
	delete uiPlayableToolBar;
	delete uiKeySigToolBar;
	delete uiTimeSigToolBar;
	delete uiClefToolBar;
	delete uiFMToolBar;
	delete uiDynamicToolBar;
	delete uiInstrumentToolBar;
	delete uiTempoToolBar;
	delete uiFermataToolBar;
	delete uiRepeatMarkToolBar;

	if(!CACanorus::mainWinCount()) // closing down
		CACanorus::cleanUp();
}

void CAMainWin::createCustomActions() {
	// Toolbars
	uiUndo = new CAUndoToolButton( QIcon("images/general/editundo.png"), CAUndoToolButton::Undo, this );
	uiUndo->setObjectName( "uiUndo" );
	uiRedo = new CAUndoToolButton( QIcon("images/general/editredo.png"), CAUndoToolButton::Redo, this );
	uiRedo->setObjectName( "uiRedo" );
	
	uiInsertToolBar = new QToolBar( tr("Insert ToolBar"), this );
	uiContextType = new CAMenuToolButton( tr("Select Context" ), 3, this );
		uiContextType->setObjectName( "uiContextType" );
		uiContextType->addButton( QIcon("images/document/staffnew.svg"), CAContext::Staff, tr("New Staff") );
		uiContextType->addButton( QIcon("images/document/lyricscontextnew.svg"), CAContext::LyricsContext, tr("New Lyrics context") );
		uiContextType->addButton( QIcon("images/document/fmcontextnew.svg"), CAContext::FunctionMarkingContext, tr("New Function Marking context") );
	uiSlurType = new CAMenuToolButton( tr("Select Slur Type"), 3, this );
		uiSlurType->setObjectName( "uiSlurType" );
		uiSlurType->addButton( QIcon("images/slur/tie.svg"), CASlur::TieType, tr("Tie") );
		uiSlurType->addButton( QIcon("images/slur/slur.svg"), CASlur::SlurType, tr("Slur") );
		uiSlurType->addButton( QIcon("images/slur/phrasingslur.svg"), CASlur::PhrasingSlurType, tr("Phrasing Slur") );
	uiClefType = new CAMenuToolButton( tr("Select Clef"), 5, this );
		uiClefType->setObjectName( "uiClefType" );
		uiClefType->addButton( QIcon("images/clef/clefg.svg"), CAClef::Treble, tr("Treble Clef") );
		uiClefType->addButton( QIcon("images/clef/clefg.svg"), CAClef::French, tr("French Clef") );
		uiClefType->addButton( QIcon("images/clef/cleff.svg"), CAClef::Bass, tr("Bass Clef") );
		uiClefType->addButton( QIcon("images/clef/cleff.svg"), CAClef::Varbaritone, tr("Varbaritone Clef") );
		uiClefType->addButton( QIcon("images/clef/cleff.svg"), CAClef::Subbass, tr("Subbass Clef") );
		uiClefType->addButton( QIcon("images/clef/clefc.svg"), CAClef::Soprano, tr("Soprano Clef") );
		uiClefType->addButton( QIcon("images/clef/clefc.svg"), CAClef::Mezzosoprano, tr("Mezzosoprano Clef") );
		uiClefType->addButton( QIcon("images/clef/clefc.svg"), CAClef::Alto, tr("Alto Clef") );
		uiClefType->addButton( QIcon("images/clef/clefc.svg"), CAClef::Tenor, tr("Tenor Clef") );
		uiClefType->addButton( QIcon("images/clef/clefc.svg"), CAClef::Baritone, tr("Baritone Clef") );
	uiTimeSigType = new CAMenuToolButton( tr("Select Time Signature" ), 3, this );
		uiTimeSigType->setObjectName( "uiTimeSigType" );
		uiTimeSigType->addButton( QIcon("images/timesig/tsc.svg"), 44 );
		uiTimeSigType->addButton( QIcon("images/timesig/tsab.svg"), 22 );
		uiTimeSigType->addButton( QIcon("images/timesig/ts34.svg"), 34 );
		uiTimeSigType->addButton( QIcon("images/timesig/ts24.svg"), 24 );
		uiTimeSigType->addButton( QIcon("images/timesig/ts38.svg"), 38 );
		uiTimeSigType->addButton( QIcon("images/timesig/ts68.svg"), 68 );
		uiTimeSigType->addButton( QIcon("images/timesig/tscustom.svg"), 0 );			
	uiBarlineType = new CAMenuToolButton( tr("Select Barline" ), 4, this );
		uiBarlineType->setObjectName( "uiBarlineType" );
		uiBarlineType->addButton( QIcon("images/barline/barlinesingle.svg"), CABarline::Single, tr("Single Barline") );
		uiBarlineType->addButton( QIcon("images/barline/barlinedouble.svg"), CABarline::Double, tr("Double Barline") );
		uiBarlineType->addButton( QIcon("images/barline/barlineend.svg"), CABarline::End, tr("End Barline") );
		uiBarlineType->addButton( QIcon("images/barline/barlinedotted.svg"), CABarline::Dotted, tr("Dotted Barline") );
		uiBarlineType->addButton( QIcon("images/barline/barlinerepeatopen.svg"), CABarline::RepeatOpen, tr("Repeat Open") );
		uiBarlineType->addButton( QIcon("images/barline/barlinerepeatclose.svg"), CABarline::RepeatClose, tr("Repeat Closed") );
		uiBarlineType->addButton( QIcon("images/barline/barlinerepeatcloseopen.svg"), CABarline::RepeatCloseOpen, tr("Repeat Closed-Open") );
	uiMarkType = new CAMenuToolButton( tr("Select Mark"), 3, this );
		uiMarkType->setObjectName( "uiMarkType" );
		uiMarkType->addButton( QIcon("images/mark/tempo/tempo.svg"),           CAMark::Tempo, tr("Tempo") );
		uiMarkType->addButton( QIcon("images/mark/tempo/rit.svg"),             CAMark::Ritardando, tr("Ritardando") );
		uiMarkType->addButton( QIcon("images/mark/tempo/accel.svg"),           CAMark::Ritardando*(-1), tr("Accellerando") );
		uiMarkType->addButton( QIcon("images/mark/dynamic/mf.svg"),            CAMark::Dynamic, tr("Dynamic") );
		uiMarkType->addButton( QIcon("images/mark/dynamic/crescendo.svg"),     CAMark::Crescendo, tr("Crescendo") );
		uiMarkType->addButton( QIcon("images/mark/dynamic/decrescendo.svg"),   CAMark::Crescendo*(-1), tr("Decrescendo") );
		uiMarkType->addButton( QIcon("images/mark/fermata/normal.svg"),        CAMark::Fermata, tr("Fermata") );
		uiMarkType->addButton( QIcon("images/mark/text.svg"),                  CAMark::Text, tr("Arbitrary Text") );
		uiMarkType->addButton( QIcon("images/mark/repeatmark/coda.svg"),       CAMark::RepeatMark, tr("Repeat Mark") );
		uiMarkType->addButton( QIcon("images/mark/pedal.svg"),                 CAMark::Pedal, tr("Instrument Change") );
		uiMarkType->addButton( QIcon("images/mark/bookmark.svg"),              CAMark::BookMark, tr("Bookmark") );
		uiMarkType->addButton( QIcon("images/mark/rehersalmark.svg"),          CAMark::RehersalMark, tr("Rehersal Mark") );
		uiMarkType->addButton( QIcon("images/mark/fingering/fingering.svg"),             CAMark::Fingering, tr("Fingering") );
		uiMarkType->addButton( QIcon("images/mark/instrumentchange.svg"),      CAMark::InstrumentChange, tr("Instrument Change") );
	uiArticulationType = new CAMenuToolButton( tr("Articulation Mark"), 6, this );
		uiArticulationType->setObjectName( "uiArticulationType" );
		uiArticulationType->addButton( QIcon("images/mark/articulation/accent.svg"),        CAArticulation::Accent,        tr("Accent") );
		uiArticulationType->addButton( QIcon("images/mark/articulation/marcato.svg"),       CAArticulation::Marcato,       tr("Marcato") );
		uiArticulationType->addButton( QIcon("images/mark/articulation/staccatissimo.svg"), CAArticulation::Staccatissimo, tr("Stacatissimo") );
		uiArticulationType->addButton( QIcon("images/mark/articulation/espressivo.svg"),    CAArticulation::Espressivo,    tr("Espressivo") );
		uiArticulationType->addButton( QIcon("images/mark/articulation/staccato.svg"),      CAArticulation::Staccato,      tr("Staccato") );
		uiArticulationType->addButton( QIcon("images/mark/articulation/tenuto.svg"),        CAArticulation::Tenuto,        tr("Tenuto") );
		uiArticulationType->addButton( QIcon("images/mark/articulation/portato.svg"),       CAArticulation::Portato,       tr("Portato") );
		uiArticulationType->addButton( QIcon("images/mark/articulation/upbow.svg"),         CAArticulation::UpBow,         tr("UpBow") );
		uiArticulationType->addButton( QIcon("images/mark/articulation/downbow.svg"),       CAArticulation::DownBow,       tr("DownBow") );
		uiArticulationType->addButton( QIcon("images/mark/articulation/flageolet.svg"),     CAArticulation::Flageolet,     tr("Flageloet") );
		uiArticulationType->addButton( QIcon("images/mark/articulation/open.svg"),          CAArticulation::Open,          tr("Open") );
		uiArticulationType->addButton( QIcon("images/mark/articulation/stopped.svg"),       CAArticulation::Stopped,       tr("Stopped") );
		uiArticulationType->addButton( QIcon("images/mark/articulation/turn.svg"),          CAArticulation::Turn,          tr("Turn") );
		uiArticulationType->addButton( QIcon("images/mark/articulation/reverseturn.svg"),   CAArticulation::ReverseTurn,   tr("ReverseTurn") );
		uiArticulationType->addButton( QIcon("images/mark/articulation/trill.svg"),         CAArticulation::Trill,         tr("Trill") );
		uiArticulationType->addButton( QIcon("images/mark/articulation/prall.svg"),         CAArticulation::Prall,         tr("Prall") );
		uiArticulationType->addButton( QIcon("images/mark/articulation/mordent.svg"),       CAArticulation::Mordent,       tr("Mordent") );
		uiArticulationType->addButton( QIcon("images/mark/articulation/prallprall.svg"),    CAArticulation::PrallPrall,    tr("Prall-Prall") );
		uiArticulationType->addButton( QIcon("images/mark/articulation/prallmordent.svg"),  CAArticulation::PrallMordent,  tr("Prall-Mordent") );
		uiArticulationType->addButton( QIcon("images/mark/articulation/upprall.svg"),       CAArticulation::UpPrall,       tr("Up-Prall") );
		uiArticulationType->addButton( QIcon("images/mark/articulation/downprall.svg"),     CAArticulation::DownPrall,     tr("Down-Prall") );
		uiArticulationType->addButton( QIcon("images/mark/articulation/upmordent.svg"),     CAArticulation::UpMordent,     tr("Up-Mordent") );
		uiArticulationType->addButton( QIcon("images/mark/articulation/downmordent.svg"),   CAArticulation::DownMordent,   tr("Down-Mordent") );
		uiArticulationType->addButton( QIcon("images/mark/articulation/pralldown.svg"),     CAArticulation::PrallDown,     tr("Prall-Down") );
		uiArticulationType->addButton( QIcon("images/mark/articulation/prallup.svg"),       CAArticulation::PrallUp,       tr("Prall-Up") );
		uiArticulationType->addButton( QIcon("images/mark/articulation/lineprall.svg"),     CAArticulation::LinePrall,     tr("Line-Prall") );
	
	uiSheetToolBar = new QToolBar( tr("Sheet ToolBar"), this );
	uiSheetName = new QLineEdit(this);
	uiSheetName->setObjectName( "uiSheetName" );
	
	uiContextToolBar = new QToolBar( tr("Context ToolBar"), this );
	uiContextName = new QLineEdit(this);
		uiContextName->setObjectName( "uiContextName" );
		uiContextName->setToolTip(tr("Context name"));
	uiStanzaNumber = new QSpinBox(this);
		uiStanzaNumber->setObjectName( "uiStanzaNumber" );
		uiStanzaNumber->setToolTip(tr("Stanza number"));
		uiStanzaNumber->hide();
	uiAssociatedVoice = new QComboBox(this);
		// Warning! disconnect and reconnect is also done in updateContextToolBar()!
		uiAssociatedVoice->setObjectName( "uiAssociatedVoice" );
		uiAssociatedVoice->setToolTip(tr("Associated voice"));
		uiAssociatedVoice->hide();
	
	uiVoiceToolBar = new QToolBar( tr("Voice ToolBar"), this );
	uiVoiceNum = new CALCDNumber( 0, 20, this, "Voice number" );
		uiVoiceNum->setObjectName( "uiVoiceNum" );
		uiVoiceNum->setToolTip(tr("Current Voice number"));
	uiVoiceInstrument = new QComboBox( this );
		uiVoiceInstrument->setObjectName("uiVoiceInstrument");
		uiVoiceInstrument->setToolTip(tr("Voice instrument"));
		uiVoiceInstrument->addItems( CACanorus::midiDevice()->GM_INSTRUMENTS );
	uiVoiceName = new QLineEdit( this );
		uiVoiceName->setObjectName( "uiVoiceName" );
		uiVoiceName->setToolTip(tr("Voice name"));
	uiVoiceStemDirection = new CAMenuToolButton( tr("Select Voice Stem Direction" ), 3, this );
		uiVoiceStemDirection->setObjectName( "uiVoiceStemDirection" );
		uiVoiceStemDirection->addButton( QIcon("images/playable/notestemneutral.svg"), CANote::StemNeutral, tr("Voice Stems Neutral") );
		uiVoiceStemDirection->addButton( QIcon("images/playable/notestemup.svg"), CANote::StemUp, tr("Voice Stems Up") );
		uiVoiceStemDirection->addButton( QIcon("images/playable/notestemdown.svg"), CANote::StemDown, tr("Voice Stems Down") );
	
	uiPlayableToolBar = new QToolBar( tr("Playable ToolBar"), this );
	uiPlayableLength = new CAMenuToolButton( tr("Select Length" ), 4, this );
		uiPlayableLength->setObjectName( "uiPlayableLength" );
		uiPlayableLength->addButton( QIcon("images/playable/n0.svg"), CANote::Breve, tr("Breve", "note") );
		uiPlayableLength->addButton( QIcon("images/playable/n1.svg"), CANote::Whole, tr("Whole", "note") );
		uiPlayableLength->addButton( QIcon("images/playable/n2.svg"), CANote::Half, tr("Half", "note") );
		uiPlayableLength->addButton( QIcon("images/playable/n4.svg"), CANote::Quarter, tr("Quarter", "note") );
		uiPlayableLength->addButton( QIcon("images/playable/n8.svg"), CANote::Eighth, tr("Eighth", "note") );
		uiPlayableLength->addButton( QIcon("images/playable/n16.svg"), CANote::Sixteenth, tr("Sixteenth", "note") );
		uiPlayableLength->addButton( QIcon("images/playable/n32.svg"), CANote::ThirtySecond, tr("ThirtySecond", "note") );
		uiPlayableLength->addButton( QIcon("images/playable/n64.svg"), CANote::SixtyFourth, tr("SixtyFourth", "note") );
	uiNoteStemDirection = new CAMenuToolButton( tr("Select Note Stem Direction" ), 4, this );
		uiNoteStemDirection->setObjectName( "uiNoteStemDirection" );
		uiNoteStemDirection->addButton( QIcon("images/playable/notestemneutral.svg"), CANote::StemNeutral, tr("Note Stem Neutral") );
		uiNoteStemDirection->addButton( QIcon("images/playable/notestemup.svg"), CANote::StemUp, tr("Note Stem Up") );
		uiNoteStemDirection->addButton( QIcon("images/playable/notestemdown.svg"), CANote::StemDown, tr("Note Stem Down") );
		uiNoteStemDirection->addButton( QIcon("images/playable/notestemvoice.svg"), CANote::StemPreferred, tr("Note Stem Preferred") );
	
	uiKeySigToolBar = new QToolBar( tr("Key Signature ToolBar"), this );
	uiKeySig = new QComboBox( this );
		uiKeySig->setObjectName("uiKeySig");
		uiKeySig->addItem( QIcon("images/accidental/accs-7.svg"), tr("C-flat major") );
		uiKeySig->addItem( QIcon("images/accidental/accs-7.svg"), tr("a-flat minor") );
		uiKeySig->addItem( QIcon("images/accidental/accs-6.svg"), tr("G-flat major") );
		uiKeySig->addItem( QIcon("images/accidental/accs-6.svg"), tr("e-flat minor") );
		uiKeySig->addItem( QIcon("images/accidental/accs-5.svg"), tr("D-flat major") );
		uiKeySig->addItem( QIcon("images/accidental/accs-5.svg"), tr("b-flat minor") );
		uiKeySig->addItem( QIcon("images/accidental/accs-4.svg"), tr("A-flat major") );
		uiKeySig->addItem( QIcon("images/accidental/accs-4.svg"), tr("f minor") );
		uiKeySig->addItem( QIcon("images/accidental/accs-3.svg"), tr("E-flat major") );
		uiKeySig->addItem( QIcon("images/accidental/accs-3.svg"), tr("c minor") );
		uiKeySig->addItem( QIcon("images/accidental/accs-2.svg"), tr("B-flat major") );
		uiKeySig->addItem( QIcon("images/accidental/accs-2.svg"), tr("g minor") );
		uiKeySig->addItem( QIcon("images/accidental/accs-1.svg"), tr("F major") );
		uiKeySig->addItem( QIcon("images/accidental/accs-1.svg"), tr("d minor") );
		uiKeySig->addItem( QIcon("images/general/none.svg"), tr("C major") );
		uiKeySig->addItem( QIcon("images/general/none.svg"), tr("a minor") );
		uiKeySig->addItem( QIcon("images/accidental/accs1.svg"), tr("G major") );
		uiKeySig->addItem( QIcon("images/accidental/accs1.svg"), tr("e minor") );
		uiKeySig->addItem( QIcon("images/accidental/accs2.svg"), tr("D major") );
		uiKeySig->addItem( QIcon("images/accidental/accs2.svg"), tr("b minor") );
		uiKeySig->addItem( QIcon("images/accidental/accs3.svg"), tr("A major") );
		uiKeySig->addItem( QIcon("images/accidental/accs3.svg"), tr("f-sharp minor") );
		uiKeySig->addItem( QIcon("images/accidental/accs4.svg"), tr("E major") );
		uiKeySig->addItem( QIcon("images/accidental/accs4.svg"), tr("c-sharp minor") );
		uiKeySig->addItem( QIcon("images/accidental/accs5.svg"), tr("B major") );
		uiKeySig->addItem( QIcon("images/accidental/accs5.svg"), tr("g-sharp minor") );
		uiKeySig->addItem( QIcon("images/accidental/accs6.svg"), tr("F-sharp major") );
		uiKeySig->addItem( QIcon("images/accidental/accs6.svg"), tr("d-sharp minor") );
		uiKeySig->addItem( QIcon("images/accidental/accs7.svg"), tr("C-sharp major") );
		uiKeySig->addItem( QIcon("images/accidental/accs7.svg"), tr("a-sharp minor") );
	
	uiTimeSigToolBar = new QToolBar( tr("Time Signature ToolBar"), this );
	uiTimeSigBeats = new QSpinBox(this);
		uiTimeSigBeats->setObjectName( "uiTimeSigBeats" );
		uiTimeSigBeats->setMinimum( 1 );
		uiTimeSigBeats->setValue( 4 );
		uiTimeSigBeats->setToolTip( tr("Number of beats") );
	uiTimeSigSlash = new QLabel( "/", this );
		uiTimeSigSlash->setObjectName( "uiTimeSigSlash" );
	uiTimeSigBeat = new QSpinBox(this);
		uiTimeSigBeat->setObjectName( "uiTimeSigBeat" );
		uiTimeSigBeat->setMinimum( 1 );
		uiTimeSigBeat->setValue( 4 );
		uiTimeSigBeat->setToolTip( tr("Beat") );
	
	uiClefToolBar = new QToolBar( tr("Clef ToolBar"), this );
	oldUiClefOffsetValue = 0;
	uiClefOffset = new QSpinBox( this );
		uiClefOffset->setObjectName("uiClefOffset");
		uiClefOffset->setMinimum( -22 );
		uiClefOffset->setMaximum( 22 );
		uiClefOffset->setValue( 0 );
		uiClefOffset->setToolTip( tr("Clef offset") );
	
	uiFMToolBar = new QToolBar( tr("Function marking ToolBar"), this );
	uiFMFunction = new CAMenuToolButton( tr("Select Function Name"), 8, this );
		uiFMFunction->setObjectName( "uiFMFunction" );
		uiFMFunction->addButton( QIcon("images/functionmark/fmt.svg"), CAFunctionMarking::T, tr("Tonic") );
		uiFMFunction->addButton( QIcon("images/functionmark/fms.svg"), CAFunctionMarking::S, tr("Subdominant") );
		uiFMFunction->addButton( QIcon("images/functionmark/fmd.svg"), CAFunctionMarking::D, tr("Dominant") );
		uiFMFunction->addButton( QIcon("images/functionmark/fmii.svg"), CAFunctionMarking::II, tr("II") );
		uiFMFunction->addButton( QIcon("images/functionmark/fmiii.svg"), CAFunctionMarking::III, tr("III") );
		uiFMFunction->addButton( QIcon("images/functionmark/fmvi.svg"), CAFunctionMarking::VI, tr("VI") );
		uiFMFunction->addButton( QIcon("images/functionmark/fmvii.svg"), CAFunctionMarking::VII, tr("VII") );
		uiFMFunction->addButton( QIcon("images/functionmark/fmk.svg"), CAFunctionMarking::K, tr("Cadenze") );
		uiFMFunction->addButton( QIcon("images/functionmark/fmot.svg"), CAFunctionMarking::T*(-1), tr("minor Tonic") );
		uiFMFunction->addButton( QIcon("images/functionmark/fmos.svg"), CAFunctionMarking::S*(-1), tr("minor Subdominant") );
		uiFMFunction->addButton( QIcon("images/functionmark/fmn.svg"), CAFunctionMarking::N, tr("Napolitan") );
		uiFMFunction->addButton( QIcon("images/functionmark/fmf.svg"), CAFunctionMarking::F, tr("Phrygian") );
		uiFMFunction->addButton( QIcon("images/functionmark/fml.svg"), CAFunctionMarking::L, tr("Lydian") );
		uiFMFunction->addButton( QIcon("images/functionmark/fmiv.svg"), CAFunctionMarking::IV, tr("IV") );
		uiFMFunction->addButton( QIcon("images/functionmark/fmv.svg"), CAFunctionMarking::V, tr("V") );
		uiFMFunction->addButton( QIcon("images/general/none.svg"), CAFunctionMarking::Undefined, tr("None") );
	uiFMChordArea = new CAMenuToolButton( tr("Select Chord Area"), 3, this );
		uiFMChordArea->setObjectName( "uiFMChordArea" );
		uiFMChordArea->addButton( QIcon("images/functionmark/fmpt.svg"), CAFunctionMarking::T, tr("Tonic") );
		uiFMChordArea->addButton( QIcon("images/functionmark/fmps.svg"), CAFunctionMarking::S, tr("Subdominant") );
		uiFMChordArea->addButton( QIcon("images/functionmark/fmpd.svg"), CAFunctionMarking::D, tr("Dominant") );
		uiFMChordArea->addButton( QIcon("images/functionmark/fmpot.svg"), CAFunctionMarking::T*(-1), tr("minor Tonic") );
		uiFMChordArea->addButton( QIcon("images/functionmark/fmpos.svg"), CAFunctionMarking::S*(-1), tr("minor Subdominant") );
		uiFMChordArea->addButton( QIcon("images/general/none.svg"), CAFunctionMarking::Undefined, tr("None") );
	uiFMTonicDegree = new CAMenuToolButton( tr("Select Tonic Degree"), 5, this );
		uiFMTonicDegree->setObjectName( "uiFMTonicDegree" );
		uiFMTonicDegree->addButton( QIcon("images/functionmark/fmt.svg"), CAFunctionMarking::T, tr("Tonic") );
		uiFMTonicDegree->addButton( QIcon("images/functionmark/fmot.svg"), CAFunctionMarking::T*(-1), tr("minor Tonic") );
		uiFMTonicDegree->addButton( QIcon("images/functionmark/fms.svg"), CAFunctionMarking::S, tr("Subdominant") );
		uiFMTonicDegree->addButton( QIcon("images/functionmark/fmos.svg"), CAFunctionMarking::S*(-1), tr("minor Subdominant") );
		uiFMTonicDegree->addButton( QIcon("images/functionmark/fmd.svg"), CAFunctionMarking::D, tr("Dominant") );
		uiFMTonicDegree->addButton( QIcon("images/functionmark/fmii.svg"), CAFunctionMarking::II, tr("II") );
		uiFMTonicDegree->addButton( QIcon("images/functionmark/fmiii.svg"), CAFunctionMarking::III, tr("III") );
		uiFMTonicDegree->addButton( QIcon("images/functionmark/fmvi.svg"), CAFunctionMarking::VI, tr("VI") );
		uiFMTonicDegree->addButton( QIcon("images/functionmark/fmvii.svg"), CAFunctionMarking::VII, tr("VII") );
	uiFMKeySig = new QComboBox( this );
		uiFMKeySig->setObjectName("uiFMKeySig");
		uiFMKeySig->addItem( QIcon("images/accidental/accs-7.svg"), tr("C-flat major") );
		uiFMKeySig->addItem( QIcon("images/accidental/accs-7.svg"), tr("a-flat minor") );
		uiFMKeySig->addItem( QIcon("images/accidental/accs-6.svg"), tr("G-flat major") );
		uiFMKeySig->addItem( QIcon("images/accidental/accs-6.svg"), tr("e-flat minor") );
		uiFMKeySig->addItem( QIcon("images/accidental/accs-5.svg"), tr("D-flat major") );
		uiFMKeySig->addItem( QIcon("images/accidental/accs-5.svg"), tr("b-flat minor") );
		uiFMKeySig->addItem( QIcon("images/accidental/accs-4.svg"), tr("A-flat major") );
		uiFMKeySig->addItem( QIcon("images/accidental/accs-4.svg"), tr("f minor") );
		uiFMKeySig->addItem( QIcon("images/accidental/accs-3.svg"), tr("E-flat major") );
		uiFMKeySig->addItem( QIcon("images/accidental/accs-3.svg"), tr("c minor") );
		uiFMKeySig->addItem( QIcon("images/accidental/accs-2.svg"), tr("B-flat major") );
		uiFMKeySig->addItem( QIcon("images/accidental/accs-2.svg"), tr("g minor") );
		uiFMKeySig->addItem( QIcon("images/accidental/accs-1.svg"), tr("F major") );
		uiFMKeySig->addItem( QIcon("images/accidental/accs-1.svg"), tr("d minor") );
		uiFMKeySig->addItem( QIcon("images/general/none.svg"), tr("C major") );
		uiFMKeySig->addItem( QIcon("images/general/none.svg"), tr("a minor") );
		uiFMKeySig->addItem( QIcon("images/accidental/accs1.svg"), tr("G major") );
		uiFMKeySig->addItem( QIcon("images/accidental/accs1.svg"), tr("e minor") );
		uiFMKeySig->addItem( QIcon("images/accidental/accs2.svg"), tr("D major") );
		uiFMKeySig->addItem( QIcon("images/accidental/accs2.svg"), tr("b minor") );
		uiFMKeySig->addItem( QIcon("images/accidental/accs3.svg"), tr("A major") );
		uiFMKeySig->addItem( QIcon("images/accidental/accs3.svg"), tr("f-sharp minor") );
		uiFMKeySig->addItem( QIcon("images/accidental/accs4.svg"), tr("E major") );
		uiFMKeySig->addItem( QIcon("images/accidental/accs4.svg"), tr("c-sharp minor") );
		uiFMKeySig->addItem( QIcon("images/accidental/accs5.svg"), tr("B major") );
		uiFMKeySig->addItem( QIcon("images/accidental/accs5.svg"), tr("g-sharp minor") );
		uiFMKeySig->addItem( QIcon("images/accidental/accs6.svg"), tr("F-sharp major") );
		uiFMKeySig->addItem( QIcon("images/accidental/accs6.svg"), tr("d-sharp minor") );
		uiFMKeySig->addItem( QIcon("images/accidental/accs7.svg"), tr("C-sharp major") );
		uiFMKeySig->addItem( QIcon("images/accidental/accs7.svg"), tr("a-sharp minor") );
	
	uiDynamicToolBar = new QToolBar( tr("Dynamic marks ToolBar"), this );
	uiDynamicText = new CAMenuToolButton( tr("Select Dynamic"), 5, this );
		uiDynamicText->setObjectName("uiDynamicText");
		uiDynamicText->addButton( QIcon("images/mark/dynamic/p.svg"), CADynamic::p, tr("Piano", "dynamics") );
		uiDynamicText->addButton( QIcon("images/mark/dynamic/pp.svg"), CADynamic::pp, tr("Pianissimo", "dynamics") );
		uiDynamicText->addButton( QIcon("images/mark/dynamic/ppp.svg"), CADynamic::ppp, tr("Pianissimo", "dynamics") );
		uiDynamicText->addButton( QIcon("images/mark/dynamic/pppp.svg"), CADynamic::pppp, tr("Pianissimo", "dynamics") );
		uiDynamicText->addButton( QIcon("images/mark/dynamic/ppppp.svg"), CADynamic::ppppp, tr("Pianissimo", "dynamics") );
		uiDynamicText->addButton( QIcon("images/mark/dynamic/f.svg"), CADynamic::f, tr("Forte", "dynamics") );
		uiDynamicText->addButton( QIcon("images/mark/dynamic/ff.svg"), CADynamic::ff, tr("Fortissimo", "dynamics") );
		uiDynamicText->addButton( QIcon("images/mark/dynamic/fff.svg"), CADynamic::fff, tr("Fortissimo", "dynamics") );
		uiDynamicText->addButton( QIcon("images/mark/dynamic/ffff.svg"), CADynamic::ffff, tr("Fortissimo", "dynamics") );
		uiDynamicText->addButton( QIcon("images/mark/dynamic/fffff.svg"), CADynamic::fffff, tr("Fortissimo", "dynamics") );
		uiDynamicText->addButton( QIcon("images/mark/dynamic/mf.svg"), CADynamic::mf, tr("Mezzo Forte", "dynamics") );
		uiDynamicText->addButton( QIcon("images/mark/dynamic/mp.svg"), CADynamic::mp, tr("Mezzo Piano", "dynamics") );
		uiDynamicText->addButton( QIcon("images/mark/dynamic/fp.svg"), CADynamic::fp, tr("Forte Piano", "dynamics") );
		uiDynamicText->addButton( QIcon("images/mark/dynamic/sf.svg"), CADynamic::sf, tr("Sforzando Forte", "dynamics") );
		uiDynamicText->addButton( QIcon("images/mark/dynamic/sp.svg"), CADynamic::sp, tr("Sforzando Piano", "dynamics") );
		uiDynamicText->addButton( QIcon("images/mark/dynamic/sfz.svg"), CADynamic::sfz, tr("Sforzando", "dynamics") );
		uiDynamicText->addButton( QIcon("images/mark/dynamic/rfz.svg"), CADynamic::rfz, tr("Rinforzando", "dynamics") );
		uiDynamicText->addButton( QIcon("images/mark/dynamic/spp.svg"), CADynamic::spp, tr("Sforzando Pianissimo", "dynamics") );
		uiDynamicText->addButton( QIcon("images/mark/dynamic/sff.svg"), CADynamic::sff, tr("Sforzando Fortissimo", "dynamics") );
		uiDynamicText->addButton( QIcon("images/general/custom.svg"), CADynamic::Custom, tr("Custom", "dynamics") );
	uiDynamicVolume = new QSpinBox(this);
		uiDynamicVolume->setObjectName( "uiDynamicVolume" );
		uiDynamicVolume->setToolTip(tr("Playback Volume"));
		uiDynamicVolume->setMinimum( 0 );
		uiDynamicVolume->setMaximum( 100 );
		uiDynamicVolume->setSuffix(" %");
	uiDynamicCustomText = new QLineEdit(this);
		uiDynamicCustomText->setObjectName( "uiDynamicCustomText" );
		uiDynamicCustomText->setToolTip(tr("Dynamic mark text"));
	
	uiInstrumentToolBar = new QToolBar( tr("Instrument ToolBar"), this );
	uiInstrumentChange = new QComboBox( this );
		uiInstrumentChange->setObjectName("uiInstrumentChange");
		uiInstrumentChange->setToolTip(tr("Instrument Change"));
		uiInstrumentChange->addItems( CACanorus::midiDevice()->GM_INSTRUMENTS );
	
	uiTempoToolBar = new QToolBar( tr("Tempo ToolBar"), this );
	uiTempoBeat = new CAMenuToolButton( tr("Select Beat"), 3, this );
		uiTempoBeat->setObjectName("uiTempoBeat");
		uiTempoBeat->addButton( QIcon("images/playable/n4.svg"), CANote::Quarter, tr("Quarter", "note") );
		uiTempoBeat->addButton( QIcon("images/playable/n2.svg"), CANote::Half, tr("Half", "note") );
		uiTempoBeat->addButton( QIcon("images/playable/n8.svg"), CANote::Eighth, tr("Eighth", "note") );
		uiTempoBeat->addButton( QIcon("images/playable/n4d.svg"), CANote::Quarter*(-1), tr("Dotted Quarter", "note") );
		uiTempoBeat->addButton( QIcon("images/playable/n2d.svg"), CANote::Half*(-1), tr("Dotted Half", "note") );
		uiTempoBeat->addButton( QIcon("images/playable/n8d.svg"), CANote::Eighth*(-1), tr("Dotted Eighth", "note") );
	uiTempoEquals = new QLabel("=", this);
		uiTempoEquals->setObjectName("uiTempoEquals");
	uiTempoBpm = new QLineEdit(this);
		uiTempoBpm->setObjectName("uiTempoBpm");
		uiTempoBpm->setToolTip(tr("Beats per minute", "tempo"));
	
	uiFermataToolBar = new QToolBar( tr("Fermata ToolBar"), this );
	uiFermataType = new CAMenuToolButton( tr("Fermata Type"), 4, this );
		uiFermataType->setObjectName("uiFermataType");
		uiFermataType->addButton( QIcon("images/mark/fermata/short.svg"), CAFermata::ShortFermata, tr("Short", "fermata") );
		uiFermataType->addButton( QIcon("images/mark/fermata/normal.svg"), CAFermata::NormalFermata, tr("Normal", "fermata") );
		uiFermataType->addButton( QIcon("images/mark/fermata/long.svg"), CAFermata::LongFermata, tr("Long", "fermata") );
		uiFermataType->addButton( QIcon("images/mark/fermata/verylong.svg"), CAFermata::VeryLongFermata, tr("Very Long", "fermata") );
	
	uiRepeatMarkToolBar = new QToolBar( tr("Repeat Mark ToolBar"), this );
	uiRepeatMarkType = new CAMenuToolButton( tr("Repeat Mark Type"), 3, this );
		uiRepeatMarkType->setObjectName("uiRepeatMarkType");
		uiRepeatMarkType->addButton( QIcon("images/mark/repeatmark/segno.svg"), CARepeatMark::Segno, tr("Segno", "repeat mark") );
		uiRepeatMarkType->addButton( QIcon("images/mark/repeatmark/coda.svg"), CARepeatMark::Coda, tr("Coda", "repeat mark") );
		uiRepeatMarkType->addButton( QIcon("images/mark/repeatmark/varcoda.svg"), CARepeatMark::VarCoda, tr("VarCoda", "repeat mark") );
		uiRepeatMarkType->addButton( QIcon("images/mark/repeatmark/dalsegno.svg"), CARepeatMark::DalSegno, tr("Dal Segno", "repeat mark") );
		uiRepeatMarkType->addButton( QIcon("images/mark/repeatmark/dalcoda.svg"), CARepeatMark::DalCoda, tr("Dal Coda", "repeat mark") );
		uiRepeatMarkType->addButton( QIcon("images/mark/repeatmark/dalvarcoda.svg"), CARepeatMark::DalVarCoda, tr("Dal VarCoda", "repeat mark") );
		uiRepeatMarkType->addButton( QIcon("images/mark/repeatmark/volta1.svg"), -2, tr("Volta 1st", "repeat mark") ); // -1 can't be used?!
		uiRepeatMarkType->addButton( QIcon("images/mark/repeatmark/volta2.svg"), -3, tr("Volta 2nd", "repeat mark") );
		uiRepeatMarkType->addButton( QIcon("images/mark/repeatmark/volta3.svg"), -4, tr("Volta 3rd", "repeat mark") );
	
	uiFingeringToolBar = new QToolBar( tr("Fingering ToolBar"), this );
	uiFinger = new CAMenuToolButton( tr("Finger"), 5, this );
		uiFinger->setObjectName("uiFinger");
		uiFinger->addButton( QIcon("images/mark/fingering/1.svg"),     CAFingering::First,  tr("First", "fingering") );
		uiFinger->addButton( QIcon("images/mark/fingering/2.svg"),     CAFingering::Second, tr("Second", "fingering") );
		uiFinger->addButton( QIcon("images/mark/fingering/3.svg"),     CAFingering::Third,  tr("Third", "fingering") );
		uiFinger->addButton( QIcon("images/mark/fingering/4.svg"),     CAFingering::Fourth, tr("Fourth", "fingering") );
		uiFinger->addButton( QIcon("images/mark/fingering/5.svg"),     CAFingering::Fifth,  tr("Fifth", "fingering") );
		uiFinger->addButton( QIcon("images/mark/fingering/0.svg"),     CAFingering::Thumb,  tr("Thumb", "fingering") );		
		uiFinger->addButton( QIcon("images/mark/fingering/lheel.svg"), CAFingering::LHeel,  tr("Left Heel", "fingering") );
		uiFinger->addButton( QIcon("images/mark/fingering/rheel.svg"), CAFingering::RHeel,  tr("Right Heel", "fingering") );
		uiFinger->addButton( QIcon("images/mark/fingering/ltoe.svg"),  CAFingering::LToe,   tr("Left Toe", "fingering") );
		uiFinger->addButton( QIcon("images/mark/fingering/rtoe.svg"),  CAFingering::RToe,   tr("Right Toe", "fingering") );
	uiFingeringOriginal = new QCheckBox( tr("Original"), this );
		uiFingeringOriginal->setObjectName("uiFingeringOriginal");
		uiFingeringOriginal->setToolTip( tr("Is the fingering original by a composer (usually written italic)", "fingering original checkbox") );
}

/*!
	Creates more complex widgets and layouts that cannot be created using Qt Designer (like adding
	custom toolbars to main window, button boxes etc.).
*/
void CAMainWin::setupCustomUi() {
	_musElementFactory = new CAMusElementFactory();
	
	// Standard Toolbar
	uiUndo->setDefaultAction( uiStandardToolBar->insertWidget( uiCut, uiUndo ) );
	uiUndo->defaultAction()->setText(tr("Undo"));
    uiUndo->defaultAction()->setShortcut(QApplication::translate("uiMainWindow", "Ctrl+Z", 0, QApplication::UnicodeUTF8));
	uiMenuEdit->insertAction( uiCut, uiUndo->defaultAction() );
	uiRedo->setDefaultAction( uiStandardToolBar->insertWidget( uiCut, uiRedo ) );
	uiRedo->defaultAction()->setText(tr("Redo"));
    uiRedo->defaultAction()->setShortcut(QApplication::translate("uiMainWindow", "Ctrl+Y", 0, QApplication::UnicodeUTF8));
	uiMenuEdit->insertAction( uiCut, uiRedo->defaultAction() );
	
	// Hide the specialized pre-created toolbars in Qt designer.
	/// \todo When Qt Designer have support for setting the visibility property, do this in Qt Designer already! -Matevz
	uiPrintToolBar->hide();
	uiFileToolBar->hide();
	uiStandardToolBar->updateGeometry();
	
	// Insert Toolbar
	uiInsertToolBar->addAction( uiSelectMode );
	uiInsertToolBar->addAction( uiEditMode );
	uiInsertToolBar->addSeparator();
	uiContextType->setDefaultAction( uiInsertToolBar->addWidget( uiContextType ) );
	uiContextType->defaultAction()->setToolTip(tr("Insert context"));
	uiContextType->setCurrentId( CAContext::Staff );
	connect( uiNewContext, SIGNAL( triggered() ), uiContextType, SLOT( click() ) );
	uiInsertToolBar->addSeparator();
	uiInsertToolBar->addAction( uiInsertPlayable );
	uiSlurType->setDefaultAction( uiInsertToolBar->addWidget( uiSlurType ) );
	uiSlurType->defaultAction()->setToolTip(tr("Insert slur"));
	uiSlurType->setCurrentId( CASlur::TieType );
	uiSlurType->defaultAction()->setCheckable(false);
	uiClefType->setDefaultAction( uiInsertToolBar->addWidget( uiClefType ) );
	uiClefType->defaultAction()->setToolTip(tr("Insert clef"));
	uiClefType->setCurrentId( CAClef::Treble );
	connect( uiInsertClef, SIGNAL( triggered() ), uiClefType, SLOT( click() ) );
	uiInsertToolBar->addAction( uiInsertKeySig );
	uiTimeSigType->setDefaultAction( uiInsertToolBar->addWidget( uiTimeSigType ) );
	uiTimeSigType->defaultAction()->setToolTip(tr("Insert time signature"));
	uiTimeSigType->setCurrentId( 44 );
	connect( uiInsertTimeSig, SIGNAL( triggered() ), uiTimeSigType, SLOT( click() ) );
	uiBarlineType->setDefaultAction( uiInsertToolBar->addWidget( uiBarlineType ) );
	uiBarlineType->defaultAction()->setToolTip(tr("Insert barline"));
	uiBarlineType->setCurrentId( CABarline::End );
	connect( uiInsertBarline, SIGNAL( triggered() ), uiBarlineType, SLOT( click() ) );
	uiMarkType->setDefaultAction( uiInsertToolBar->addWidget( uiMarkType ) );
	uiMarkType->defaultAction()->setToolTip( tr("Insert mark") );
	uiMarkType->setCurrentId( CAMark::Dynamic );
	connect( uiInsertMark, SIGNAL( triggered() ), uiMarkType, SLOT( click() ) );
	uiArticulationType->setDefaultAction( uiInsertToolBar->addWidget( uiArticulationType ) );
	uiArticulationType->defaultAction()->setToolTip( tr("Insert articulation mark") );
	uiArticulationType->setCurrentId( CAArticulation::Accent );
	connect( uiInsertArticulation, SIGNAL( triggered() ), uiArticulationType, SLOT( click() ) );
	uiInsertToolBar->addAction( uiInsertSyllable );
	uiInsertToolBar->addAction( uiInsertFM );
	
	if(qApp->isRightToLeft())
		addToolBar(Qt::RightToolBarArea, uiInsertToolBar);
	else
		addToolBar(Qt::LeftToolBarArea, uiInsertToolBar);

	// Sheet Toolbar
	uiSheetToolBar->addAction( uiNewSheet );
	uiSheetToolBar->addWidget( uiSheetName );
	uiSheetToolBar->addAction( uiRemoveSheet );
	uiSheetToolBar->addAction( uiSheetProperties );
	addToolBar(Qt::TopToolBarArea, uiSheetToolBar);
	
	// Context Toolbar
	uiContextToolBar->addWidget( uiContextName );
	uiStanzaNumberAction = uiContextToolBar->addWidget( uiStanzaNumber );
	uiAssociatedVoiceAction = uiContextToolBar->addWidget( uiAssociatedVoice );
	uiContextToolBar->addAction( uiRemoveContext );
	uiContextToolBar->addAction( uiContextProperties );
	addToolBar(Qt::TopToolBarArea, uiContextToolBar);
	
	// Playable Toolbar
	uiPlayableLength->setDefaultAction( uiPlayableToolBar->addWidget( uiPlayableLength ) );
	uiPlayableLength->defaultAction()->setToolTip(tr("Playable length"));
	uiPlayableLength->defaultAction()->setCheckable(false);
	uiPlayableLength->setCurrentId( CANote::Quarter );
	uiPlayableToolBar->addAction( uiAccsVisible );
	uiNoteStemDirection->setDefaultAction( uiPlayableToolBar->addWidget(uiNoteStemDirection ) );
	uiNoteStemDirection->defaultAction()->setToolTip(tr("Note stem direction"));		
	uiNoteStemDirection->defaultAction()->setCheckable(false);
	uiNoteStemDirection->setCurrentId( CANote::StemPreferred );
	uiPlayableToolBar->addAction( uiHiddenRest );
	addToolBar(Qt::TopToolBarArea, uiPlayableToolBar);
	
	// KeySig Toolbar
	uiKeySigToolBar->addWidget( uiKeySig );
	addToolBar(Qt::TopToolBarArea, uiKeySigToolBar);
	
	// Clef Toolbar
	uiClefToolBar->addWidget( uiClefOffset );
	addToolBar(Qt::TopToolBarArea, uiClefToolBar);
	
	// TimeSig Toolbar
	uiTimeSigToolBar->addWidget( uiTimeSigBeats );
	uiTimeSigToolBar->addWidget( uiTimeSigSlash );
	uiTimeSigToolBar->addWidget( uiTimeSigBeat );
	addToolBar(Qt::TopToolBarArea, uiTimeSigToolBar);
	
	// Voice Toolbar
	uiVoiceToolBar->addAction( uiNewVoice );
	uiVoiceToolBar->addWidget( uiVoiceNum );
	uiVoiceToolBar->addWidget( uiVoiceName );
	uiVoiceToolBar->addWidget( uiVoiceInstrument );
	uiVoiceToolBar->addAction( uiRemoveVoice );
	uiVoiceStemDirection->setDefaultAction( uiVoiceToolBar->addWidget( uiVoiceStemDirection ) );
	uiVoiceStemDirection->defaultAction()->setToolTip(tr("Voice stem direction"));
	uiVoiceStemDirection->defaultAction()->setCheckable(false);
	uiVoiceToolBar->addAction( uiVoiceProperties );
	addToolBar(Qt::TopToolBarArea, uiVoiceToolBar);
	
	// Function marking Toolbar
	uiFMFunction->setDefaultAction( uiFMToolBar->addWidget( uiFMFunction ) );
	uiFMFunction->defaultAction()->setToolTip( tr("Function marking") );
	uiFMFunction->setCurrentId( CAFunctionMarking::T );
	uiFMFunction->defaultAction()->setCheckable( false );
	uiFMChordArea->setDefaultAction( uiFMToolBar->addWidget( uiFMChordArea ) );
	uiFMChordArea->defaultAction()->setToolTip( tr("Function marking chord area") );
	uiFMChordArea->setCurrentId( CAFunctionMarking::T );
	uiFMChordArea->defaultAction()->setCheckable( false );
	uiFMTonicDegree->setDefaultAction( uiFMToolBar->addWidget( uiFMTonicDegree ) );
	uiFMTonicDegree->defaultAction()->setCheckable( false );
	uiFMTonicDegree->defaultAction()->setToolTip( tr("Function marking tonic degree") );
	uiFMTonicDegree->setCurrentId( CAFunctionMarking::T );
	uiFMToolBar->addAction( uiFMEllipse );
	uiFMToolBar->addWidget( uiFMKeySig );
	connect( uiFMKeySig, SIGNAL( activated(int) ), this, SLOT( on_uiKeySig_activated(int) ) );
	addToolBar(Qt::TopToolBarArea, uiFMToolBar);
	
	// Dynamic marks toolbar
	uiDynamicText->setDefaultAction( uiDynamicToolBar->addWidget( uiDynamicText ) );
	uiDynamicText->defaultAction()->setToolTip( tr("Predefined dynamic mark") );
	uiDynamicText->setCurrentId( CADynamic::mf );
	uiDynamicText->defaultAction()->setCheckable( false );
	uiDynamicToolBar->addWidget( uiDynamicVolume );
	uiDynamicToolBar->addWidget( uiDynamicCustomText );
	addToolBar(Qt::TopToolBarArea, uiDynamicToolBar);
	
	// Instrument tool bar
	uiInstrumentToolBar->addWidget( uiInstrumentChange );
	addToolBar(Qt::TopToolBarArea, uiInstrumentToolBar);
	
	// Tempo tool bar
	uiTempoBeat->setDefaultAction( uiTempoToolBar->addWidget( uiTempoBeat ) );
	uiTempoBeat->defaultAction()->setCheckable(false);
	uiTempoBeat->defaultAction()->setToolTip(tr("Beat", "tempo"));
	uiTempoToolBar->addWidget( uiTempoEquals );
	uiTempoToolBar->addWidget( uiTempoBpm );
	addToolBar(Qt::TopToolBarArea, uiTempoToolBar);
	
	// Fermata tool bar
	uiFermataType->setDefaultAction( uiFermataToolBar->addWidget( uiFermataType ) );
	uiFermataType->defaultAction()->setCheckable(false);
	uiFermataType->defaultAction()->setToolTip(tr("Fermata Type", "fermata"));
	addToolBar(Qt::TopToolBarArea, uiFermataToolBar);
	
	// Repeat Mark tool bar
	uiRepeatMarkType->setDefaultAction( uiRepeatMarkToolBar->addWidget( uiRepeatMarkType ) );
	uiRepeatMarkType->defaultAction()->setCheckable(false);
	uiRepeatMarkType->defaultAction()->setToolTip(tr("Repeat Mark Type", "repeat mark"));
	addToolBar(Qt::TopToolBarArea, uiRepeatMarkToolBar);
	
	// Fingering tool bar
	uiFinger->setDefaultAction( uiFingeringToolBar->addWidget( uiFinger ) );
	uiFinger->defaultAction()->setCheckable(false);
	uiFinger->defaultAction()->setToolTip(tr("Finger", "fingering"));
	uiFingeringOriginal->setChecked(false);
	uiFingeringToolBar->addWidget( uiFingeringOriginal );
	addToolBar(Qt::TopToolBarArea, uiFingeringToolBar);
	
	// Mutual exclusive groups
	uiInsertGroup = new QActionGroup( this );
	uiInsertGroup->addAction( uiSelectMode );
	uiInsertGroup->addAction( uiEditMode );
	uiInsertGroup->addAction( uiNewContext );
	uiInsertGroup->addAction( uiContextType->defaultAction() );
	uiInsertGroup->addAction( uiInsertPlayable );
	uiInsertGroup->addAction( uiSlurType->defaultAction() );
	uiInsertGroup->addAction( uiInsertClef );
	uiInsertGroup->addAction( uiClefType->defaultAction() );
	uiInsertGroup->addAction( uiInsertTimeSig );
	uiInsertGroup->addAction( uiTimeSigType->defaultAction() );
	uiInsertGroup->addAction( uiInsertKeySig );
	uiInsertGroup->addAction( uiInsertBarline );
	uiInsertGroup->addAction( uiBarlineType->defaultAction() );
	uiInsertGroup->addAction( uiMarkType->defaultAction() );
	uiInsertGroup->addAction( uiInsertMark );
	uiInsertGroup->addAction( uiArticulationType->defaultAction() );
	uiInsertGroup->addAction( uiInsertArticulation );
	uiInsertGroup->addAction( uiInsertSyllable );
	uiInsertGroup->addAction( uiInsertFM );
	uiInsertGroup->setExclusive( true );
	
	uiInsertToolBar->hide();
	uiSheetToolBar->hide();
	uiContextToolBar->hide();
	uiPlayableToolBar->hide();
	uiTimeSigToolBar->hide();
	uiKeySigToolBar->hide();
	uiClefToolBar->hide();
	uiFMToolBar->hide();
	uiDynamicToolBar->hide();
	uiInstrumentToolBar->hide();
	uiTempoToolBar->hide();
	uiFermataToolBar->hide();
	uiRepeatMarkToolBar->hide();
	uiFingeringToolBar->hide();
}

void CAMainWin::newDocument() {
	stopPlayback();
	// clear GUI before clearing the data part!
	clearUI();
	
	// clear the data part
	if ( document() && (CACanorus::mainWinCount(document()) == 1) ) {
		CACanorus::undo()->deleteUndoStack( document() ); 
		delete document();
	}
	
	setDocument(new CADocument());
	CACanorus::undo()->createUndoStack( document() );
	restartTimeEditedTime();
	
#ifdef USE_PYTHON
	QList<PyObject*> argsPython;
	argsPython << CASwigPython::toPythonObject(document(), CASwigPython::Document);
	CASwigPython::callFunction(CACanorus::locateResource("scripts/newdocument.py").at(0), "newDefaultDocument", argsPython);
#endif
	
	// call local rebuild only because no other main windows share the new document
	rebuildUI();
	
	// select the first context automatically
	if ( document()->sheetCount() && document()->sheetAt(0)->contextCount() )
		currentScoreViewPort()->selectContext( document()->sheetAt(0)->contextAt(0) );
	updateToolBars();
}

/*!
	This adds a tab to tabWidget and creates a single score viewport of the sheet.
	It does not add the sheet to the document.
*/
void CAMainWin::addSheet(CASheet *s) {
	CAScoreViewPort *v = new CAScoreViewPort(s, 0);
	initViewPort( v );
	
	CAViewPortContainer *vpc = new CAViewPortContainer( 0 );
	vpc->addViewPort( v );
	_viewPortContainerList << vpc;
	_sheetMap[vpc] = s;
	
	uiTabWidget->addTab(vpc, s->name());
	uiTabWidget->setCurrentIndex(uiTabWidget->count()-1);
	setCurrentViewPortContainer( vpc );
	
	updateToolBars();
}

/*!
	Deletes all viewports (and their drawable content), disconnects all signals and resets all
	buttons and modes.
	
	This function deletes the current main window's GUI only (drawable elements). All the data
	classes (staffs, notes, rests) should stay intact. Use delete document() to free the data
	part of Canorus as well.
*/
void CAMainWin::clearUI() {
	if(_viewPortList.size() > 1)
		on_uiUnsplitAll_triggered();
	
	// Delete all view port containers
	while (uiTabWidget->count()) {
		CAViewPortContainer *vpc = static_cast<CAViewPortContainer*>(uiTabWidget->currentWidget());
		uiTabWidget->removeTab( 0 );
		delete vpc;
	}
	
	// Delete all viewports
	while(!_viewPortList.isEmpty())
		delete _viewPortList.takeFirst();
	
	_sheetMap.clear();
	setCurrentViewPort( 0 );
	uiSelectMode->trigger(); // select mode
}

/*!
	Called when the current sheet is switched in the tab widget.
	\warning This method is only called when the index of the selected tab changes. If you remove the current tab and the next selected tab gets the same index, this slot isn't called!
*/
void CAMainWin::on_uiTabWidget_currentChanged(int idx) {
	setCurrentViewPortContainer( static_cast<CAViewPortContainer*>(uiTabWidget->currentWidget()) );
	if (currentViewPortContainer())
		setCurrentViewPort( currentViewPortContainer()->currentViewPort() );
	
	updateToolBars();
}

void CAMainWin::on_uiFullscreen_toggled(bool checked) {
	if (checked)
		this->showFullScreen();
	else
		this->showNormal();
}

void CAMainWin::on_uiHiddenRest_toggled( bool checked ) {
	if ( mode()==InsertMode )
		musElementFactory()->setRestType( checked ? CARest::Hidden : CARest::Normal );
	else if (mode()==SelectMode || mode()==EditMode) {
		CAScoreViewPort *v = currentScoreViewPort();
		if ( v && v->selection().size() ) {
			CARest *rest = dynamic_cast<CARest*>(v->selection().at(0)->musElement());
			if ( rest ) {
				rest->setRestType( checked ? CARest::Hidden : CARest::Normal );
				CACanorus::rebuildUI( document(), currentSheet() );
			}
		}
	}}

void CAMainWin::on_uiSplitHorizontally_triggered() {
	CAViewPort *v = currentViewPortContainer()->splitHorizontally();
	if(!v)
		return;
	
	initViewPort( v );
	
	uiUnsplitAll->setEnabled(true);
	uiCloseCurrentView->setEnabled(true);
}

void CAMainWin::on_uiSplitVertically_triggered() {
	CAViewPort *v = currentViewPortContainer()->splitVertically();
	if(!v)
		return;
	
	initViewPort( v );
	
	uiUnsplitAll->setEnabled(true);
	uiCloseCurrentView->setEnabled(true);
}

void CAMainWin::on_uiUnsplitAll_triggered() {
	currentViewPortContainer()->unsplitAll();
	uiCloseCurrentView->setEnabled(false);
	uiUnsplitAll->setEnabled(false);
	setCurrentViewPort( currentViewPortContainer()->currentViewPort() );
}

void CAMainWin::on_uiCloseCurrentView_triggered() {
	currentViewPortContainer()->unsplit();
	
	if (currentViewPortContainer()->viewPortList().size() == 1)
	{
		uiCloseCurrentView->setEnabled(false);
		uiUnsplitAll->setEnabled(false);
	}
	setCurrentViewPort( currentViewPortContainer()->currentViewPort() );
}

void CAMainWin::on_uiCloseDocument_triggered() {
	if ( CACanorus::mainWinCount(document()) == 1 ) {
		CACanorus::undo()->deleteUndoStack( document() ); 
		delete document();
	}
	setDocument( 0 );
	rebuildUI();
}

/*!
	Shows the current score in CanorusML syntax in a new or the current viewport.
*/
void CAMainWin::on_uiCanorusMLSource_triggered() {
	CASourceViewPort *v = new CASourceViewPort(document(), 0);
	initViewPort( v );
	currentViewPortContainer()->addViewPort( v );
	
	uiUnsplitAll->setEnabled(true);
	uiCloseCurrentView->setEnabled(true);
}

void CAMainWin::on_uiNewViewport_triggered() {
	CAViewPort *v = currentViewPort()->clone( 0 );
	initViewPort( v );
	v->show();
	v->setGeometry(v->x(), v->y(), CAViewPort::DEFAULT_VIEWPORT_WIDTH, CAViewPort::DEFAULT_VIEWPORT_HEIGHT);
}

/*!
	Links the newly created viewport with the main window:
		- Adds the viewport to the viewport list
		- Connects its signals to main windows' slots.
		- Sets the icon, focus policy and sets the focus.
		- Sets the currentViewPort but not currentViewPortContainer
*/
void CAMainWin::initViewPort(CAViewPort *v) {
	_viewPortList << v;
	QList<QString> paths = CACanorus::locateResource("images/clogosm.png");
	if ( !paths.size() )
		paths = CACanorus::locateResource("ui/images/clogosm.png");
	
	if ( paths.size() )
		v->setWindowIcon(QIcon( paths[0] ));
	
	switch (v->viewPortType()) {
		case CAViewPort::ScoreViewPort: {
			connect( v, SIGNAL(CAMousePressEvent(QMouseEvent *, QPoint, CAScoreViewPort *)),
			         this, SLOT(scoreViewPortMousePress(QMouseEvent *, QPoint, CAScoreViewPort *)) );
			connect( v, SIGNAL(CAMouseMoveEvent(QMouseEvent *, QPoint, CAScoreViewPort *)),
			         this, SLOT(scoreViewPortMouseMove(QMouseEvent *, QPoint, CAScoreViewPort *)) );
			connect( v, SIGNAL(CAMouseReleaseEvent(QMouseEvent *, QPoint, CAScoreViewPort *)),
			         this, SLOT(scoreViewPortMouseRelease(QMouseEvent *, QPoint, CAScoreViewPort *)) );
			connect( v, SIGNAL(CAWheelEvent(QWheelEvent *, QPoint, CAScoreViewPort *)),
			         this, SLOT(scoreViewPortWheel(QWheelEvent *, QPoint, CAScoreViewPort *)) );
			connect( v, SIGNAL(CAKeyPressEvent(QKeyEvent *, CAScoreViewPort *)),
			         this, SLOT(scoreViewPortKeyPress(QKeyEvent *, CAScoreViewPort *)) );
			connect( static_cast<CAScoreViewPort*>(v)->textEdit(), SIGNAL(CAKeyPressEvent(QKeyEvent*)),
			         this, SLOT(onTextEditKeyPressEvent(QKeyEvent*)) );
			connect( v, SIGNAL(selectionChanged()),
			         this, SLOT(onScoreViewPortSelectionChanged()) );
			break;
		}
		case CAViewPort::SourceViewPort: {
			connect(v, SIGNAL(CACommit(QString, CASourceViewPort*)), this, SLOT(sourceViewPortCommit(QString, CASourceViewPort*)));
			break;
		}
	}
	
	v->setFocusPolicy(Qt::ClickFocus);
	v->setFocus();
	setCurrentViewPort(v);
	setMode(mode());	// updates the new viewport border settings	
}

/*!
	Returns the currently selected context in the current view port or 0 if no contexts are selected.
*/
CAContext *CAMainWin::currentContext() {
	if ( currentViewPort() &&
	     (currentViewPort()->viewPortType() == CAViewPort::ScoreViewPort) &&
	     (static_cast<CAScoreViewPort*>(currentViewPort())->currentContext())
	   ) {
		return static_cast<CAScoreViewPort*>(currentViewPort())->currentContext()->context();
	} else
		return 0;
}

/*!
	Returns the pointer to the currently active voice or 0, if All voices are selected or the current context is not a staff at all.
*/
CAVoice *CAMainWin::currentVoice() {
	CAStaff *staff = currentStaff();
	if (staff) {
		if ( uiVoiceNum->getRealValue() &&
		     uiVoiceNum->getRealValue() <= staff->voiceCount())
			return staff->voiceAt( uiVoiceNum->getRealValue() - 1);
	}
	
	return 0;
}

/*!
	Creates a new main window sharing the current document.
*/
void CAMainWin::on_uiNewWindow_triggered() {
	CAMainWin *newMainWin = new CAMainWin();
	newMainWin->setDocument(document());
	newMainWin->rebuildUI();
	newMainWin->show();
}

void CAMainWin::on_uiNewDocument_triggered() {
	newDocument();
}

void CAMainWin::on_uiUndo_toggled( bool checked, int row ) {
	stopPlayback();
	if ( document() ) {
		for (int i=0; i<=row; i++) {
			CACanorus::undo()->undo( document() );
		}
	}
	CACanorus::rebuildUI( document(), 0 );
	on_uiVoiceNum_valChanged( uiVoiceNum->getRealValue() ); // updates current voice in score viewport
}

void CAMainWin::on_uiRedo_toggled( bool checked, int row ) {
	stopPlayback();
	if ( document() ) {
		for (int i=0; i<=row; i++) {
			CACanorus::undo()->redo( document() );
		}
	}
	CACanorus::rebuildUI( document(), 0 );
	on_uiVoiceNum_valChanged( uiVoiceNum->getRealValue() ); // updates current voice in score viewport
}

/*!
	Enables or Disabled undo/redo buttons if there are undo/redo commands on the undo stack.
	
	\sa CACanorus::undoStack()
*/
void CAMainWin::updateUndoRedoButtons() {
	if ( CACanorus::undo()->canUndo( document() ) )
		uiUndo->defaultAction()->setEnabled(true);
	else
		uiUndo->defaultAction()->setEnabled(false);
	
	if ( CACanorus::undo()->canRedo( document() ) )
		uiRedo->defaultAction()->setEnabled(true);
	else
		uiRedo->defaultAction()->setEnabled(false);
}

/*!
	Adds a new empty sheet.
*/
void CAMainWin::on_uiNewSheet_triggered() {
	stopPlayback();
	CACanorus::undo()->createUndoCommand( document(), tr("new sheet", "undo") );
	document()->addSheetByName( tr("Sheet%1").arg(QString::number(document()->sheetCount()+1)) );
	CACanorus::undo()->pushUndoCommand();
	CACanorus::rebuildUI(document());
	uiTabWidget->setCurrentIndex(uiTabWidget->count()-1);
}

/*!
	Adds a new voice to the staff.
*/
void CAMainWin::on_uiNewVoice_triggered() {
	CAStaff *staff = currentStaff();
	int voiceNumber = staff->voiceCount()+1;
	CANote::CAStemDirection stemDirection;
	if ( voiceNumber == 1 )
		stemDirection = CANote::StemNeutral;
	else {
		staff->voiceAt(0)->setStemDirection( CANote::StemUp );
		stemDirection = CANote::StemDown;
	}
	
	CACanorus::undo()->createUndoCommand( document(), tr("new voice", "undo") );
	if (staff) {
		staff->addVoice(new CAVoice( staff->name() + tr("Voice%1").arg( staff->voiceCount()+1 ), staff, stemDirection, voiceNumber ));
		staff->synchronizeVoices();
	}
	
	CACanorus::undo()->pushUndoCommand();
	CACanorus::rebuildUI(document(), currentSheet());
	uiVoiceNum->setRealValue( staff->voiceCount() );
}

/*!
	Removes the current voice from the staff and deletes its contents.
*/
void CAMainWin::on_uiRemoveVoice_triggered() {
	CAVoice *voice = currentVoice();
	if (voice) {
		// Last voice cannot be deleted
		if (voice->staff()->voiceCount()==1) {
			int ret = QMessageBox::critical(
				this, tr("Canorus"),
				tr("Cannot delete the last voice in the staff!")
			);
			return;
		}
		
		int ret = QMessageBox::warning(
			this, tr("Canorus"),
			tr("Are you sure do you want to delete voice\n%1 and all its notes?").arg(voice->name()),
			QMessageBox::Yes | QMessageBox::No,
			QMessageBox::No);
		
		if (ret == QMessageBox::Yes) {
			stopPlayback();
			CACanorus::undo()->createUndoCommand( document(), tr("voice removal", "undo") );
			currentScoreViewPort()->clearSelection();
			uiVoiceNum->setRealValue( voice->staff()->voiceCount()-1 );
			delete voice; // also removes voice from the staff
			CACanorus::undo()->pushUndoCommand();
			CACanorus::rebuildUI(document(), currentSheet());
		}
	}
}

/*!
	Removes the current context from the sheet and all its contents.
*/
void CAMainWin::on_uiRemoveContext_triggered() {
	CAContext *context = currentContext();
	if (context) {
		int ret = QMessageBox::warning(
			this, tr("Canorus"),
			tr("Are you sure do you want to delete context\n%1 and all its contents?").arg(context->name()),
			QMessageBox::Yes | QMessageBox::No,
			QMessageBox::No);
		
		if (ret == QMessageBox::Yes) {
			stopPlayback();
			CACanorus::undo()->createUndoCommand( document(), tr("context removal", "undo") );
			CASheet *sheet = context->sheet();
			sheet->removeContext(context);
			CACanorus::undo()->pushUndoCommand();			
			CACanorus::rebuildUI(document(), currentSheet());
			delete context;
		}
	}
}

void CAMainWin::on_uiContextType_toggled(bool checked, int buttonId) {
	if (checked)
		setMode( InsertMode );
}

void CAMainWin::on_uiSelectMode_toggled(bool checked) {
	if (checked)
		setMode( SelectMode );
}

void CAMainWin::on_uiEditMode_toggled(bool checked) {
	if (checked)
		setMode( EditMode );
}

/*!
	Sets the current mode and updates the GUI and toolbars.
*/
void CAMainWin::setMode(CAMode mode) {
	_mode = mode;
	
	switch (mode) {
		case SelectMode: {
			for (int i=0; i<_viewPortList.size(); i++) {
				if ( currentScoreViewPort() ) {
					if (!currentScoreViewPort()->playing())
						currentScoreViewPort()->unsetBorder();
					currentScoreViewPort()->setShadowNoteVisible(false);
					if (currentScoreViewPort()->textEditVisible())
						currentScoreViewPort()->removeTextEdit();
					statusBar()->showMessage("");
					musElementFactory()->setMusElementType( CAMusElement::Undefined );
					uiVoiceNum->setRealValue( 0 );
					_viewPortList[i]->repaint();
				}
			}
			break;
		}
		case InsertMode: {
			QPen p;
			p.setColor(Qt::blue);
			p.setWidth(3);
			
			if ( currentScoreViewPort() )
				currentScoreViewPort()->setShadowNoteVisible((musElementFactory()->musElementType() == CAMusElement::Note) ? true : false); /// \todo Set other mouse cursors

			for (int i=0; i<_viewPortList.size(); i++) {
				if (_viewPortList[i]->viewPortType()==CAViewPort::ScoreViewPort) {
					if (!((CAScoreViewPort*)_viewPortList[i])->playing())
						((CAScoreViewPort*)_viewPortList[i])->setBorder(p);
					((CAScoreViewPort*)_viewPortList[i])->repaint();
				}
			}

			break;
		}
		case EditMode: {
			QPen p;
			p.setColor(Qt::red);
			p.setWidth(3);
			for (int i=0; i<_viewPortList.size(); i++) {
				if (_viewPortList[i]->viewPortType()==CAViewPort::ScoreViewPort) {
					CAScoreViewPort *sv = static_cast<CAScoreViewPort*>(_viewPortList[i]);
					if (!sv->playing())
						(sv->setBorder(p));
					
					sv->setShadowNoteVisible(false);
					sv->repaint();
				}
			}
			
			if (currentScoreViewPort() && currentScoreViewPort()->selection().size()) {
				CAMusElement *elt = currentScoreViewPort()->selection().front()->musElement();
				if ( elt->musElementType()==CAMusElement::Syllable ||
				     elt->musElementType()==CAMusElement::Mark && (static_cast<CAMark*>(elt)->markType()==CAMark::Text || static_cast<CAMark*>(elt)->markType()==CAMark::BookMark)
				) {
					currentScoreViewPort()->createTextEdit(currentScoreViewPort()->selection().front());
				} else {
					currentScoreViewPort()->removeTextEdit();
				}
			}
		}
	}	// switch (mode)
	updateToolBars();
	if ( currentScoreViewPort() && !currentScoreViewPort()->textEditVisible() ||
	     !currentScoreViewPort() && currentViewPort() )
		currentViewPort()->setFocus();
}

/*!
	Rebuilds the GUI from data.
	
	This method is called eg. when multiple viewports share the same data and a change has been made (eg. a
	note pitch has changed or a new element added). ViewPorts content is repositioned and redrawn (CAEngraver
	creates CADrawable elements for every score viewport, sources are updated in source viewports etc.).
	
	\a sheet argument is a pointer to the data sheet where the change occured. This way only viewports showing
	the given sheet are updated which speeds up the process.
	If \a sheet argument is null, all viewports are rebuilt, but the viewports contents, number and locations
	remain the same.
	
	If \a repaint is True (default) the rebuilt viewports are also repainted. If False, viewports content is
	only created but not yet drawn. This is useful when multiple operations which could potentially change the
	content are to happen and we want to actually draw it only at the end.
*/
void CAMainWin::rebuildUI(CASheet *sheet, bool repaint) {
	if (rebuildUILock()) return;
	
	setRebuildUILock( true );
	if (document()) {
		for (int i=0; i<_viewPortList.size(); i++) {
			if ((!_viewPortList[i]->parent()) || // the viewport was removed (unsplitted). 
				(sheet && _viewPortList[i]->viewPortType()==CAViewPort::ScoreViewPort &&
				    static_cast<CAScoreViewPort*>(_viewPortList[i])->sheet()!=sheet)
			)
				continue;
			
			_viewPortList[i]->rebuild();
			
			if (_viewPortList[i]->viewPortType() == CAViewPort::ScoreViewPort)
				static_cast<CAScoreViewPort*>(_viewPortList[i])->checkScrollBars();
				
			if (repaint)
				_viewPortList[i]->repaint();
		}
	} else {
		clearUI();
	}
	updateToolBars();
	setRebuildUILock( false );
}

/*!
	Rebuilds the GUI from data.
	
	This method is called eg. when multiple viewports share the same data and a change has been made (eg. a
	note pitch has changed or a new element added). ViewPorts content is repositioned and redrawn (CAEngraver
	creates CADrawable elements for every score viewport, sources are updated in source viewports etc.).
	
	This method in comparison to CAMainWin::rebuildUI(CASheet *s, bool repaint) rebuilds the whole GUI from
	scratch and creates new viewports for the sheets. This method is called for example when a new document
	is created or opened.
	
	If \a repaint is True (default) the rebuilt viewports are also repainted. If False, viewports content is
	only created but not yet drawn. This is useful when multiple operations which could potentially change the
	content are to happen and we want to actually draw it only at the end.
*/
void CAMainWin::rebuildUI(bool repaint) {
	if (rebuildUILock()) return;
	
	setRebuildUILock( true );
	if (document()) {
		int curIndex = uiTabWidget->currentIndex();
		
		// save the current state of viewports
		QList<QRect> worldCoordsList;
		for (int i=0; i<_viewPortList.size(); i++)
			if (_viewPortList[i]->viewPortType() == CAViewPort::ScoreViewPort)
				worldCoordsList << static_cast<CAScoreViewPort*>(_viewPortList[i])->worldCoords();
		
		clearUI();
		for (int i=0; i<document()->sheetCount(); i++) {
			addSheet(document()->sheetAt(i));
			
			// restore the current state of viewports
			if ( _viewPortList[i]->viewPortType() == CAViewPort::ScoreViewPort &&
			     i < worldCoordsList.size() )
				static_cast<CAScoreViewPort*>(_viewPortList[i])->setWorldCoords(worldCoordsList[i]);
		}
		
		for (int i=0; i<_viewPortList.size(); i++) {
			_viewPortList[i]->rebuild();
			
			if (_viewPortList[i]->viewPortType() == CAViewPort::ScoreViewPort)
				static_cast<CAScoreViewPort*>(_viewPortList[i])->checkScrollBars();
				
			if (repaint)
				_viewPortList[i]->repaint();
		}
		
		if ( curIndex<uiTabWidget->count() )
			uiTabWidget->setCurrentIndex(curIndex);
	} else {
		clearUI();
	}
	updateToolBars();
	setRebuildUILock( false );
}

/*!
	Processes the mouse press event \a e with world coordinates \a coords of the score viewport \a v.
	Any action happened in any of the viewports are always linked to these main window slots.
	
	\sa CAScoreViewPort::mousePressEvent(), scoreViewPortMouseMove(), scoreViewPortWheel(), scoreViewPortKeyPress()
*/
void CAMainWin::scoreViewPortMousePress(QMouseEvent *e, const QPoint coords, CAScoreViewPort *v) {
	setCurrentViewPort( v );
	currentViewPortContainer()->setCurrentViewPort( currentViewPort() );
	
	CADrawableContext *prevContext = v->currentContext();
	v->selectCElement(coords.x(), coords.y());
		
	QList<CADrawableMusElement*> l = v->musElementsAt( coords.x(), coords.y() );
	CADrawableMusElement *newlySelectedElement=0;
	int idx=-1;
	
	if (l.size() > 0) { // multiple elements can share the same coordinates
		if ( (v->selection().size() > 0) && (!v->selection().contains(l.front())) ) {
			if (e->modifiers()!=Qt::ShiftModifier)
				v->clearSelection();
			v->addToSelection( newlySelectedElement = l[0] );      // if the previous selection was not a single element or if the new list doesn't contain the selection set the first element in the available list to the selection
		} else {
			if (e->modifiers()==Qt::ShiftModifier && v->selection().size()) {
				v->removeFromSelection(l[0]); // shift used on an already selected element - toggle selection
			} else {
				idx = (v->selection().size()?l.indexOf(v->selection().front()):-1);
				v->clearSelection();
				v->addToSelection( newlySelectedElement = l[((++idx < l.size()) ? idx : 0)] ); // if there are two or more elements with the same coordinates, select the next one (behind it). This way, you can click multiple times on the same place and you'll always select the other element.
			}
		}
	} else if (e->modifiers()==Qt::NoModifier) { // no elements at that coordinates
		v->clearSelection();
	}
	
	// always select the context the current element belongs to
	if ( newlySelectedElement && (mode()!=InsertMode || !uiInsertPlayable->isChecked() ) )
		v->setCurrentContext( newlySelectedElement->drawableContext() );
	
	if ( v->currentContext() && prevContext != v->currentContext() && mode()!=InsertMode) {	// new context was selected
		// voice number widget
		if (v->currentContext()->context()->contextType() == CAContext::Staff) {
			uiVoiceNum->setRealValue(0);
			uiVoiceNum->setMax(static_cast<CAStaff*>(v->currentContext()->context())->voiceCount());
		}
	} else if ( prevContext != v->currentContext() && uiInsertPlayable->isChecked() ) { // but insert playable mode is active and context should remain the same
		v->setCurrentContext( prevContext );
	}
	
	switch ( mode() ) {
		case SelectMode:
		case EditMode: {
			v->clearSelectionRegionList();
			
			if ( v->selection().size() ) {
				CADrawableMusElement *dElt = v->selection().front();
				CAMusElement *elt = dElt->musElement();
				if (!elt) break;
				
				if ( mode()==EditMode &&
				     (elt->musElementType()==CAMusElement::Syllable || elt->musElementType()==CAMusElement::Mark && static_cast<CAMark*>(elt)->markType()==CAMark::Text)
				   ){
					v->createTextEdit( dElt );
				}
				
				// debug
				std::cout << "drawableMusElement: " << dElt << ", x,y=" << dElt->xPos() << "," << dElt->yPos() << ", w,h=" << dElt->width() << "," << dElt->height() << ", dContext=" << dElt->drawableContext() << std::endl;
				std::cout << "musElement: " << elt << ", timeStart=" << elt->timeStart() << ", timeEnd=" << elt->timeEnd() << ", dContext = " << v->selection().front()->drawableContext() << ", context=" << elt->context();
				if (elt->isPlayable()) {
					std::cout << ", voice=" << ((CANote*)elt)->isPartOfTheChord() << ", voiceNr=" << ((CAPlayable*)elt)->voice()->voiceNumber() << ", idxInVoice=" << ((CAPlayable*)elt)->voice()->indexOf(elt);
					std::cout << ", voiceStaff=" << ((CAPlayable*)elt)->voice()->staff();
					if (elt->musElementType()==CAMusElement::Note)
						std::cout << ", pitch=" << ((CANote*)elt)->pitch();
				}
				std::cout << std::endl;
			} else {
				v->removeTextEdit();
			}
			
			break;
		}
		case InsertMode: {
			// Insert context
			if (uiContextType->isChecked()) {
				// Add new Context
				CAContext *newContext;
				CADrawableContext *dupContext = v->nearestUpContext(coords.x(), coords.y());
				switch(uiContextType->currentId()) {
					case CAContext::Staff: {
						CACanorus::undo()->createUndoCommand( document(), tr("new staff", "undo"));
						v->sheet()->insertContextAfter(
							dupContext?dupContext->context():0,
							newContext = new CAStaff(
								tr("Staff%1").arg(v->sheet()->staffCount()+1),
								v->sheet()
							)
						);
						static_cast<CAStaff*>(newContext)->addVoice();
						break;
					}
					case CAContext::LyricsContext: {
						CACanorus::undo()->createUndoCommand( document(), tr("new lyrics context", "undo"));
						
						int stanza=1;
						if (dupContext && dupContext->context() && dupContext->context()->contextType()==CAContext::LyricsContext)
							stanza = static_cast<CALyricsContext*>(dupContext->context())->stanzaNumber()+1;
							
						v->sheet()->insertContextAfter(
							dupContext?dupContext->context():0,
							newContext = new CALyricsContext(
								tr("LyricsContext%1").arg(v->sheet()->contextCount()+1),
								1,
								(v->sheet()->voiceList().size()?v->sheet()->voiceList().at(0):0)
							)
						);
						
						break;
					}
					case CAContext::FunctionMarkingContext: {
						CACanorus::undo()->createUndoCommand( document(), tr("new function marking context", "undo"));
						v->sheet()->insertContextAfter(
							dupContext?dupContext->context():0,
							newContext = new CAFunctionMarkingContext(
								tr("FunctionMarkingContext%1").arg(v->sheet()->contextCount()+1),
								v->sheet()
							)
						);
						break;
					}
				}
				CACanorus::undo()->pushUndoCommand();
				CACanorus::rebuildUI(document(), v->sheet());
				
				v->selectContext(newContext);
				if (newContext->contextType()==CAContext::Staff) {
					uiVoiceNum->setMax( 1 );
					uiVoiceNum->setRealValue( 0 );
				}
				uiSelectMode->toggle();
				v->repaint();
				break;
			} else
			// Insert music element
			if (uiInsertPlayable->isChecked()) {
				// Add Note/Rest
				if (e->button()==Qt::RightButton && musElementFactory()->musElementType()==CAMusElement::Note)
					// place a rest when using right mouse button and note insertion is selected
					musElementFactory()->setMusElementType( CAMusElement::Rest );
				// show the dotted shadow note
				currentScoreViewPort()->setShadowNoteDotted( musElementFactory()->playableDotted() );
			}
			
			insertMusElementAt( coords, v );
			
			if ( musElementFactory()->musElementType()==CAMusElement::Rest )
			     musElementFactory()->setMusElementType( CAMusElement::Note );
			
			// Insert Syllable or Text
			if ( uiInsertSyllable->isChecked() || uiMarkType->isChecked() && (musElementFactory()->markType()==CAMark::Text || musElementFactory()->markType()==CAMark::BookMark) && !v->selection().isEmpty() ) {
				v->createTextEdit( v->selection().front() );
			} else {
				v->removeTextEdit();
			}
			
			break;
		}
	}
	
	CAPluginManager::action("onScoreViewPortClick", document(), 0, 0, this);
	
	updateToolBars();
	v->repaint();
}

/*!
	Processes the mouse move event \a e with coordinates \a coords of the score viewport \a v.
	Any action happened in any of the viewports are always linked to its main window slots.
	
	\sa CAScoreViewPort::mouseMoveEvent(), scoreViewPortMousePress(), scoreViewPortWheel(), scoreViewPortKeyPress()
*/
void CAMainWin::scoreViewPortMouseMove(QMouseEvent *e, QPoint coords, CAScoreViewPort *c) {
	if ( mode() == SelectMode && c->resizeDirection()!=CADrawable::Undefined ) {
		int time = c->coordsToTime(coords.x());
		time -= (time%CAPlayable::playableLengthToTimeLength(CAPlayable::Sixteenth)); // round timelength to eighth notes length
	if ( c->resizeDirection()==CADrawable::Right && (time > c->selection().at(0)->musElement()->timeStart()) ) {
			c->selection().at(0)->musElement()->setTimeLength( time - c->selection().at(0)->musElement()->timeStart() );
			c->selection().at(0)->setWidth( c->timeToCoords(time) - c->selection().at(0)->xPos() );
			c->repaint();
		} else
		if ( c->resizeDirection()==CADrawable::Left && (time < c->selection().at(0)->musElement()->timeEnd()) ) {
			c->selection().at(0)->musElement()->setTimeLength( c->selection().at(0)->musElement()->timeEnd() - time );
			c->selection().at(0)->musElement()->setTimeStart( time );
			c->selection().at(0)->setXPos( c->timeToCoords(time) );
			c->selection().at(0)->setWidth( c->timeToCoords(c->selection().at(0)->musElement()->timeEnd()) - c->timeToCoords(time) );
			c->repaint();
		}
	} else
	if ( (mode() == InsertMode && musElementFactory()->musElementType() == CAMusElement::Note) ) {
		CADrawableStaff *s;
		if (c->currentContext()?(c->currentContext()->drawableContextType() == CADrawableContext::DrawableStaff):0)
			s = (CADrawableStaff*)c->currentContext(); 
		else
			return;
		
		if ( musElementFactory()->musElementType() == CAMusElement::Note || 
             musElementFactory()->musElementType() == CAMusElement::Rest) {
			c->setShadowNoteVisible(true);
        }
        
		// calculate the musical pitch out of absolute world coordinates and the current clef
		int pitch = s->calculatePitch(coords.x(), coords.y());
		
		// write into the main window's status bar the note pitch name
		int iNoteAccs = s->getAccs(coords.x(), pitch) + musElementFactory()->noteExtraAccs();
		musElementFactory()->setNoteAccs( iNoteAccs );
		statusBar()->showMessage(CANote::generateNoteName(pitch, iNoteAccs));
		c->setShadowNoteAccs(iNoteAccs);
		c->repaint();
	} else
	if ( mode()!=InsertMode  && e->buttons()==Qt::LeftButton ) { // multiple selection
		c->clearSelectionRegionList();
		int x=c->lastMousePressCoords().x(), y=c->lastMousePressCoords().y(),
		    w=coords.x()-c->lastMousePressCoords().x(), h=coords.y()-c->lastMousePressCoords().y();
		if (w<0) { x+=w; w*=(-1); } // user selected from right to left
		if (h<0) { y+=h; h*=(-1); } // user selected from bottom to top
		QRect selectionRect( x, y, w, h );
		
		QList<CADrawableContext*> dcList = c->findContextsInRegion( selectionRect );
		for (int i=0; i<dcList.size(); i++) {
			QList<CADrawableMusElement*> musEltList = dcList[i]->findInRange( selectionRect.x(), selectionRect.x() + selectionRect.width() );
			for (int j=0; j<musEltList.size(); j++)
				if (musEltList[j]->drawableMusElementType()==CADrawableMusElement::DrawableSlur)
					musEltList.removeAt(j--);
			
			if (musEltList.size()) {
				c->addSelectionRegion( QRect(musEltList.front()->xPos(), dcList[i]->yPos(),
				                             musEltList.back()->xPos()+musEltList.back()->width()-musEltList.front()->xPos(), dcList[i]->height()) );
			}
		}
		c->repaint();
	}
}

/*!
	Processes the mouse move event \a e with coordinates \a coords of the score viewport \a v.
	Any action happened in any of the viewports are always linked to its main window slots.
	
	\sa CAScoreViewPort::mouseReleaseEvent(), scoreViewPortMousePress(), scoreViewPortMouseMove(), scoreViewPortWheel(), scoreViewPortKeyPress()
*/
void CAMainWin::scoreViewPortMouseRelease(QMouseEvent *e, QPoint coords, CAScoreViewPort *c) {
	if ( mode() != InsertMode  && c->lastMousePressCoords()!=coords ) { // area was selected
		c->clearSelectionRegionList();
		
		if (e->modifiers()==Qt::NoModifier)
			c->clearSelection();
		
		int x=c->lastMousePressCoords().x(), y=c->lastMousePressCoords().y(),
		    w=coords.x()-c->lastMousePressCoords().x(), h=coords.y()-c->lastMousePressCoords().y();
		if (w<0) { x+=w; w*=(-1); } // user selected from right to left
		if (h<0) { y+=h; h*=(-1); } // user selected from bottom to top
		QRect selectionRect( x, y, w, h );
		
		QList<CADrawableContext*> dcList = c->findContextsInRegion( selectionRect );
		for (int i=0; i<dcList.size(); i++) {
			QList<CADrawableMusElement*> musEltList = dcList[i]->findInRange( selectionRect.x(), selectionRect.x() + selectionRect.width() );
			if ( c->selectedVoice() && dcList[i]->context()!=c->selectedVoice()->staff() )
				continue;
			
			for (int j=0; j<musEltList.size(); j++)
				if ((!musEltList[j]->isSelectable()) || 
					(c->selectedVoice() && musEltList[j]->musElement()->isPlayable() &&
						static_cast<CAPlayable*>(musEltList[j]->musElement())->voice()!=c->selectedVoice()) ||
					(musEltList[j]->drawableMusElementType()==CADrawableMusElement::DrawableSlur)
				)
					musEltList.removeAt(j--);
			c->addToSelection(musEltList);
		}
		c->repaint();
	}
}

/*!
	Processes the mouse wheel event \a e with coordinates \a coords of the score viewport \a v.
	Any action happened in any of the viewports are always linked to its main window slots.
	
	\sa CAScoreViewPort::wheelEvent(), scoreViewPortMousePress(), scoreViewPortMouseMove(), scoreViewPortKeyPress()
*/
void CAMainWin::scoreViewPortWheel(QWheelEvent *e, QPoint coords, CAScoreViewPort *sv) {
	setCurrentViewPort( sv );
	
	int val;
	switch (e->modifiers()) {
		case Qt::NoModifier:			//scroll horizontally
			sv->setWorldX( sv->worldX() - (int)((0.5*e->delta()) / sv->zoom()), _animatedScroll );
			break;
		case Qt::AltModifier:			//scroll horizontally, fast
			sv->setWorldX( sv->worldX() - (int)(e->delta() / sv->zoom()), _animatedScroll );
			break;
		case Qt::ShiftModifier:			//scroll vertically
			sv->setWorldY( sv->worldY() - (int)((0.5*e->delta()) / sv->zoom()), _animatedScroll );
			break;
		case 0x0A000000://SHIFT+ALT		//scroll vertically, fast
			sv->setWorldY( sv->worldY() - (int)(e->delta() / sv->zoom()), _animatedScroll );
			break;
		case Qt::ControlModifier:		//zoom
			if (e->delta() > 0)
				sv->setZoom( sv->zoom()*1.1, coords.x(), coords.y(), _animatedScroll );
			else
				sv->setZoom( sv->zoom()/1.1, coords.x(), coords.y(), _animatedScroll );
			
			break;
	}
	
	sv->repaint();
}

/*!
	Processes the key press event \a e of the score viewport \a v.
	Any action happened in any of the viewports are always linked to its main window slots.
	
	\sa CAScoreViewPort::keyPressEvent(), scoreViewPortMousePress(), scoreViewPortMouseMove(), scoreViewPortWheel()
*/
void CAMainWin::scoreViewPortKeyPress(QKeyEvent *e, CAScoreViewPort *v) {
	setCurrentViewPort( v );
	
	switch (e->key()) {
		// Music editing keys
		case Qt::Key_Right: {
			// select next music element
			v->selectNextMusElement( e->modifiers()==Qt::ShiftModifier );
			v->repaint();
			break;
		}
		
		case Qt::Key_Left: {
			// select previous music element
			v->selectPrevMusElement( e->modifiers()==Qt::ShiftModifier );
			v->repaint();
			break;
		}
		
		case Qt::Key_B: {
			// place a barline
			CADrawableContext *drawableContext;
			drawableContext = v->currentContext();
			
			if ( (!drawableContext) || (drawableContext->context()->contextType() != CAContext::Staff) )
				return;
			
			CAStaff *staff = static_cast<CAStaff*>(drawableContext->context());
			CAMusElement *right = 0;
			if (!v->selection().isEmpty())
				right = staff->next(v->selection().back()->musElement());
			
			CACanorus::undo()->createUndoCommand( document(), tr("insert barline", "undo") );
			CABarline *bar = new CABarline(
				CABarline::Single,
				staff,
				0
			);
			
			if (currentVoice()) {
				currentVoice()->insert( right, bar ); // insert the barline in all the voices, timeStart is set
			} else {
				if ( right && right->isPlayable() )
					static_cast<CAPlayable*>(right)->voice()->insert( right, bar );
				else
					staff->voiceAt(0)->insert( right, bar );
			}
			
			staff->synchronizeVoices();
			
			CACanorus::undo()->pushUndoCommand();
			CACanorus::rebuildUI(document(), v->sheet());
			v->selectMElement(bar);
			v->repaint();
			break;
		}
		
		case Qt::Key_Up: {
			if (mode() == SelectMode) {	// select the upper music element
				v->selectUpMusElement();
				v->repaint();
			} else if ((mode() == InsertMode) || (mode() == EditMode)) {
				bool rebuild=false;
				if (v->selection().size())
					CACanorus::undo()->createUndoCommand( document(), tr("rise note", "undo") );
				
				for (int i=0; i<v->selection().size(); i++) {
					CADrawableMusElement *elt = v->selection().at(i);
					
					// pitch note for one step higher
					if (elt->drawableMusElementType() == CADrawableMusElement::DrawableNote) {
						CANote *note = (CANote*)elt->musElement();
						note->setPitch(note->pitch()+1);
						CACanorus::undo()->pushUndoCommand();
						rebuild = true;
					}
				}
				if (rebuild)
					CACanorus::rebuildUI(document(), currentSheet());
			}
			break;
		}
		
		case Qt::Key_Down: {
			if (mode() == SelectMode) {	// select the upper music element
				v->selectUpMusElement();
				v->repaint();
			} else if ((mode() == InsertMode) || (mode() == EditMode)) {
				bool rebuild = false;
				if (v->selection().size())
					CACanorus::undo()->createUndoCommand( document(), tr("lower note", "undo") );
				
				for (int i=0; i<v->selection().size(); i++) {
					CADrawableMusElement *elt = v->selection().at(i);
					
					// pitch note for one step higher
					if (elt->drawableMusElementType() == CADrawableMusElement::DrawableNote) {
						CANote *note = (CANote*)elt->musElement();
						note->setPitch(note->pitch()-1);
						CACanorus::undo()->pushUndoCommand();
						rebuild = true;
					}
				}
				CACanorus::rebuildUI(document(), currentSheet());
			}
			break;
		}
		
		case Qt::Key_Plus: {
			if (mode()==InsertMode) {
				musElementFactory()->addNoteExtraAccs(1); musElementFactory()->addNoteAccs(1);
				v->setDrawShadowNoteAccs(musElementFactory()->noteExtraAccs()!=0);
				v->setShadowNoteAccs(musElementFactory()->noteAccs());
				v->repaint();
			} else if (mode()==EditMode) {
				if (!v->selection().isEmpty()) {
					CAMusElement *elt = v->selection().front()->musElement();
					if (elt->musElementType()==CAMusElement::Note) {
						CACanorus::undo()->createUndoCommand( document(), tr("add sharp", "undo") );
						if ( ((CANote*)elt)->accidentals() < 2 )		// limit the amount of accidentals
							((CANote*)elt)->setAccidentals(((CANote*)elt)->accidentals()+1);
						CACanorus::undo()->pushUndoCommand();
						CACanorus::rebuildUI(document(), ((CANote*)elt)->voice()->staff()->sheet());
					}
				}
			}
			break;
		}
		
		case Qt::Key_Minus: {
			if (mode()==InsertMode) {
				musElementFactory()->subNoteExtraAccs(1); musElementFactory()->subNoteAccs(1);
				v->setDrawShadowNoteAccs(musElementFactory()->noteExtraAccs()!=0);
				v->setShadowNoteAccs(musElementFactory()->noteAccs());
				v->repaint();
			} else if (mode()==EditMode) {
				if (!v->selection().isEmpty()) {
					CAMusElement *elt = v->selection().front()->musElement();
					if (elt->musElementType()==CAMusElement::Note) {
						CACanorus::undo()->createUndoCommand( document(), tr("add flat", "undo") );
						if ( ((CANote*)elt)->accidentals() > -2 )		// limit the amount of accidentals
							((CANote*)elt)->setAccidentals(((CANote*)elt)->accidentals()-1);
						CACanorus::undo()->pushUndoCommand();
						CACanorus::rebuildUI(document(), ((CANote*)elt)->voice()->staff()->sheet());
					}
				}
			}
			break;
		}
		
		case Qt::Key_Period: {
			if (mode()==InsertMode) {
				musElementFactory()->addPlayableDotted( 1, musElementFactory()->playableLength() );
				currentScoreViewPort()->setShadowNoteDotted( musElementFactory()->playableDotted() );
				v->repaint();
			} else if (mode()==EditMode) {
				if (!((CAScoreViewPort*)v)->selection().isEmpty()) {
					CACanorus::undo()->createUndoCommand( document(), tr("set dotted", "undo") );
					CAPlayable *p = dynamic_cast<CAPlayable*>(currentScoreViewPort()->selection().front()->musElement());
					
					if (p) {
						CAMusElement *next=0;
						int oldLength = p->timeLength();
						if ( p->musElementType()==CAMusElement::Note ) {                   // change the length of the whole chord
							QList<CANote*> chord = static_cast<CANote*>(p)->getChord();
							for (int i=0; i<chord.size(); i++) {
								next = p->voice()->next(p);
								p->voice()->remove(chord[i]);
								chord[i]->setDotted( (p->dotted()+1)%4 );
							}
							p->voice()->insert( next, chord[0], false );
							for (int i=1; i<chord.size(); i++) {
								p->voice()->insert( chord[0], chord[i], true );
							}
						} else if ( p->musElementType()==CAMusElement::Rest ) {
							next = p->voice()->next(p);
							p->voice()->remove(p);
							p->setDotted( (p->dotted()+1)%4 );
							p->voice()->insert( next, p );
						}
						
						int newLength = p->timeLength();
						if (newLength<oldLength) { // insert rests, if the new length is shorter to keep consistency
							QList<CARest*> rests = CARest::composeRests( oldLength - newLength, p->timeStart()+p->timeLength(), p->voice(), CARest::Normal );
							for (int i=rests.size()-1; i>=0; i--) {
								p->voice()->insert( next, rests[i] ); // insert rests from shortest to longest
							}
						} else {
							p->staff()->synchronizeVoices();
						}
						
						for (int j=0; j<p->voice()->lyricsContextList().size(); j++) { // reposit syllables
							p->voice()->lyricsContextList().at(j)->repositSyllables();
						}
						
						CACanorus::undo()->pushUndoCommand();
						CACanorus::rebuildUI(document(), p->staff()->sheet());
					}
				}
			}
			break;
		}
		
		case Qt::Key_Delete:
		case Qt::Key_Backspace:
			deleteSelection( v, e->modifiers()==Qt::ShiftModifier, e->modifiers()==Qt::ShiftModifier, true );
			break;
		
		// Mode keys
		case Qt::Key_Escape:
			if (mode()==SelectMode) {
				v->clearSelection();
				v->setCurrentContext( 0 );
			}
			uiVoiceNum->setRealValue( 0 );
			uiSelectMode->trigger();
			break;
		case Qt::Key_I:
			uiInsertPlayable->trigger();
			break;
		case Qt::Key_E:
			uiEditMode->trigger();
			break;
		case Qt::Key_Space:
			uiPlayFromSelection->trigger();
			break;
		
		// Note length keys
		case Qt::Key_1:
			if (mode()!=EditMode)
				uiInsertPlayable->setChecked(true);
			uiPlayableLength->setCurrentId( CANote::Whole, true );
			musElementFactory()->setPlayableDotted( 0 );
			break;
		case Qt::Key_2:
			if (mode()!=EditMode)
				uiInsertPlayable->setChecked(true);
			uiPlayableLength->setCurrentId( CANote::Half, true );
			musElementFactory()->setPlayableDotted( 0 );
			break;
		case Qt::Key_4:
			if (mode()!=EditMode)
				uiInsertPlayable->setChecked(true);
			uiPlayableLength->setCurrentId( CANote::Quarter, true );
			musElementFactory()->setPlayableDotted( 0 );
			break;
		case Qt::Key_5:
			if (mode()!=EditMode)
				uiInsertPlayable->setChecked(true);
			uiPlayableLength->setCurrentId( CANote::Eighth, true );
			musElementFactory()->setPlayableDotted( 0 );
			break;
		case Qt::Key_6:
			if (mode()!=EditMode)
				uiInsertPlayable->setChecked(true);
			uiPlayableLength->setCurrentId( CANote::Sixteenth, true );
			musElementFactory()->setPlayableDotted( 0 );
			break;
		case Qt::Key_7:
			if (mode()!=EditMode)
				uiInsertPlayable->setChecked(true);
			uiPlayableLength->setCurrentId( CANote::ThirtySecond, true );
			musElementFactory()->setPlayableDotted( 0 );
			break;
		case Qt::Key_8:
			if (mode()!=EditMode)
				uiInsertPlayable->setChecked(true);
			uiPlayableLength->setCurrentId( CANote::SixtyFourth, true );
			musElementFactory()->setPlayableDotted( 0 );
			break;
		case Qt::Key_9:
			if (mode()!=EditMode)
				uiInsertPlayable->setChecked(true);
			uiPlayableLength->setCurrentId( CANote::HundredTwentyEighth, true );
			musElementFactory()->setPlayableDotted( 0 );
			break;
		case Qt::Key_0:
			if (mode()!=EditMode)
				uiInsertPlayable->setChecked(true);
			uiPlayableLength->setCurrentId( CANote::Breve, true );
			musElementFactory()->setPlayableDotted( 0 );
			break;
	}
	
	updateToolBars();
}

/*!
	This method places the currently prepared music element in CAMusElementFactory to the staff or
	voice, dependent on the music element type and the viewport coordinates.
*/
void CAMainWin::insertMusElementAt(const QPoint coords, CAScoreViewPort *v) {
	CADrawableContext *drawableContext = v->currentContext();
	
	CAStaff *staff=0;
	CADrawableStaff *drawableStaff = 0;
	if (drawableContext) {
		drawableStaff = dynamic_cast<CADrawableStaff*>(drawableContext);
		staff = dynamic_cast<CAStaff*>(drawableContext->context());
	}
	
	CADrawableMusElement *drawableRight = v->nearestRightElement(coords.x(), coords.y());
	
	CAMusElement *right=0;
	if ( drawableRight )
		right = drawableRight->musElement();
	
	bool success=false;
	
	if (!drawableContext)
		return;
	
	CACanorus::undo()->createUndoCommand( document(), tr("insertion of music element", "undo") );
	
	switch ( musElementFactory()->musElementType() ) {
		case CAMusElement::Clef: {
			if (staff)
				success = musElementFactory()->configureClef( staff , right );
			break;
		}
		case CAMusElement::KeySignature: {
			if ( staff )
				success = musElementFactory()->configureKeySignature( staff, right );
			break;
		}
		case CAMusElement::TimeSignature: {
			if ( staff )
				success = musElementFactory()->configureTimeSignature( staff, right );
			break;
		}
		case CAMusElement::Barline: {
			if ( staff )
				success = musElementFactory()->configureBarline( staff, right );
			break;
		}
		case CAMusElement::Mark: {
			if ( v->musElementsAt( coords.x(), coords.y() ).size() )
				success = musElementFactory()->configureMark( v->musElementsAt( coords.x(), coords.y() )[0]->musElement() );
			break;
		}
		case CAMusElement::Note: { // Do we really need to do all that here??
			CAVoice *voice = currentVoice();
			
			if ( !voice )
				break;
			
			CADrawableMusElement *left = v->nearestLeftElement( coords.x(), coords.y(), voice ); // use nearestLeft search because it searches left borders
			CADrawableMusElement *dright = v->nearestRightElement( coords.x(), coords.y(), voice );
			
			if ( left && left->musElement() && left->musElement()->musElementType() == CAMusElement::Note &&
			     left->xPos() <= coords.x() && (left->width() + left->xPos() >= coords.x()) ) {
				
				// user clicked inside x borders of the note - add a note to the chord
				
				if ( voice->containsPitch( drawableStaff->calculatePitch(coords.x(), coords.y()), left->musElement()->timeStart() ) )
					break;	// user clicked on an already placed note or wanted to place illegal length (not the one the chord is of) - return and do nothing
				
				success = musElementFactory()->configureNote( drawableStaff->calculatePitch(coords.x(), coords.y()), voice, left->musElement(), true );
			} else
			if ( left && left->musElement() && left->musElement()->musElementType() == CAMusElement::Rest &&
			     left->xPos() <= coords.x() && (left->width() + left->xPos() >= coords.x()) ) {
				
				// user clicked inside x borders of the rest - replace the rest/rests with the note
				
				int timeSum = left->musElement()->timeLength();
				int timeLength = CAPlayable::playableLengthToTimeLength( musElementFactory()->playableLength(), musElementFactory()->playableDotted() );
				CAMusElement *next;
				while ( (next=voice->next(left->musElement())) &&
				        next->musElementType() == CAMusElement::Rest &&
				        timeSum < timeLength ) {
					voice->remove(next);
					timeSum += next->timeLength();
				}
				
				voice->remove( left->musElement() );
				
				if (timeSum - timeLength > 0) {
					// we removed too many rests - insert the delta of the missing rests
					QList<CARest*> rests = CARest::composeRests( timeSum - timeLength, next?next->timeStart():voice->lastTimeEnd(), voice, CARest::Normal );
					for (int i=rests.size()-1; i>=0; i--)
						voice->insert(next, rests[i]);
					next = rests[0];
				}
				
				success = musElementFactory()->configureNote( drawableStaff->calculatePitch(coords.x(), coords.y()), voice, next, false );
			} else {
				
				// user clicked outside x borders of the note or rest
				
				success = musElementFactory()->configureNote( drawableStaff->calculatePitch(coords.x(), coords.y()), voice, dright?dright->musElement():0, false );
			}
			
			if ( success ) {
				musElementFactory()->setNoteExtraAccs( 0 );
				v->setDrawShadowNoteAccs( false ); 
				v->setShadowNoteDotted( musElementFactory()->playableDotted() );
			}
			break;
		}
		case CAMusElement::Rest: {
			CAVoice *voice = currentVoice();
			
			if ( !voice )
				break;
			
			CADrawableMusElement *left = v->nearestLeftElement( coords.x(), coords.y(), voice ); // use nearestLeft search because it searches left borders
			CADrawableMusElement *dright = v->nearestRightElement( coords.x(), coords.y(), voice );
			
			if ( left && left->musElement() && left->musElement()->isPlayable() &&
			     left->xPos() <= coords.x() && (left->width() + left->xPos() >= coords.x()) ) {
				
				// user clicked inside x borders of the rest - replace the rest/rests with the new rest
				
				int timeSum = left->musElement()->timeLength();
				int timeLength = CAPlayable::playableLengthToTimeLength( musElementFactory()->playableLength(), musElementFactory()->playableDotted() );
				CAMusElement *next;
				while ( (next=voice->next(left->musElement())) &&
				        next->musElementType() == CAMusElement::Rest &&
				        timeSum < timeLength ) {
					voice->remove(next);
					timeSum += next->timeLength();
				}
				
				voice->remove( left->musElement() );
				
				if (timeSum - timeLength > 0) {
					// we removed too many rests - insert the delta of the missing rests
					QList<CARest*> rests = CARest::composeRests( timeSum - timeLength, next?next->timeStart():voice->lastTimeEnd(), voice, CARest::Normal );
					for (int i=rests.size()-1; i>=0; i--)
						voice->insert(next, rests[i]);
					next = rests[0];
				}
				
				success = musElementFactory()->configureRest( voice, next );
			} else {
				success = musElementFactory()->configureRest( voice, dright?dright->musElement():0 );
			}
			
			if (success)
				v->setShadowNoteDotted( musElementFactory()->playableDotted() );
			
			break;
		}
		case CAMusElement::Slur: {
			// Insert tie, slur or phrasing slur
			if ( v->selection().size() ) { // start note has to always be selected
				CANote *noteStart = (currentScoreViewPort()->selection().front()->musElement()?dynamic_cast<CANote*>(currentScoreViewPort()->selection().front()->musElement()):0);
				CANote *noteEnd = (currentScoreViewPort()->selection().back()->musElement()?dynamic_cast<CANote*>(currentScoreViewPort()->selection().back()->musElement()):0);
				
				// Insert Tie
				if ( noteStart && musElementFactory()->slurType()==CASlur::TieType ) {
					noteEnd = 0; // find a fresh next note
					QList<CANote*> noteList = noteStart->voice()->getNoteList();
					
					if ( noteStart->tieStart() ) {
						break; // return, if the tie already exists
					} else {
						// create a new tie
						for (int i=0; i<noteList.count() && noteList[i]->timeStart()<=noteStart->timeEnd(); i++) {
							if ( noteList[i]->timeStart()==noteStart->timeEnd() && noteList[i]->pitch()==noteStart->pitch() ) {
								noteEnd = noteList[i];
								break;
							}
						}
					}
					success = musElementFactory()->configureSlur( staff, noteStart, noteEnd );
				} else
				// Insert slur or phrasing slur
				if ( noteStart && noteEnd && noteStart != noteEnd && (musElementFactory()->slurType()==CASlur::SlurType || musElementFactory()->slurType()==CASlur::PhrasingSlurType) ) {
					if (noteStart->isPartOfTheChord()) noteStart = noteStart->getChord().at(0);
					if (noteEnd->isPartOfTheChord()) noteEnd = noteEnd->getChord().at(0);
					QList<CANote*> noteList = noteStart->voice()->getNoteList();
					int end = noteList.indexOf(noteEnd);
					for (int i=noteList.indexOf(noteStart); i<=end; i++)
						if ( musElementFactory()->slurType()==CASlur::SlurType && (noteList[i]->slurStart() || noteList[i]->slurEnd()) ||
						     musElementFactory()->slurType()==CASlur::PhrasingSlurType && (noteList[i]->phrasingSlurStart() || noteList[i]->phrasingSlurEnd()) )
							return;
					
					if (musElementFactory()->slurType()==CASlur::SlurType && (noteStart->slurStart() || noteEnd->slurEnd()) ||
					    musElementFactory()->slurType()==CASlur::PhrasingSlurType && (noteStart->phrasingSlurStart() || noteEnd->phrasingSlurEnd()))
						break; // return, if the slur already exist
					success = musElementFactory()->configureSlur( staff, noteStart, noteEnd );
				}
			}
			break;
		}
		case CAMusElement::FunctionMarking: {
			// Insert function marking
			if (drawableContext->context()->contextType()==CAContext::FunctionMarkingContext) {
				CAFunctionMarkingContext *fmc = static_cast<CAFunctionMarkingContext*>(drawableContext->context());
				CADrawableMusElement *dLeft = v->nearestLeftElement(coords.x(), coords.y(), false);
				int timeStart = 0;
				if ( dLeft ) // find the nearest left element from the cursor
					timeStart = dLeft->musElement()->timeStart();
				QList<CAPlayable*> chord = currentSheet()->getChord(timeStart);
				int timeLength = chord.size()?chord[0]->timeLength():256;
				for (int i=0; i<chord.size(); i++) // find the shortest note in the chord
					if (chord[i]->timeLength()-(timeStart-chord[i]->timeStart())<timeLength)
						timeLength = chord[i]->timeLength()-(timeStart-chord[i]->timeStart());
				
				success = musElementFactory()->configureFunctionMarking( fmc, timeStart, timeLength );
			}
			break;
		}
	}
	
	if (success) {
		if (staff)
			staff->synchronizeVoices();
		
		CACanorus::undo()->pushUndoCommand();
		CACanorus::rebuildUI(document(), v->sheet());
		v->selectMElement( musElementFactory()->musElement() );
		musElementFactory()->emptyMusElem();
	}
}

/*!
	Main window's key press event.
	
	\sa viewPortKeyPressEvent()
*/
void CAMainWin::keyPressEvent(QKeyEvent *e) {
}

/*!
	Called when selection in score viewports is changed.
*/
void CAMainWin::onScoreViewPortSelectionChanged() {
	if (static_cast<CAScoreViewPort*>(sender())->selection().size()) {
		uiCopy->setEnabled(true);
		uiCut->setEnabled(true);
	} else {
		uiCopy->setEnabled(false);
		uiCut->setEnabled(false);		
	}
}

/*!
	Called every second when timeEditedTimer has timeout.
	Increases the locally stored time the document is being edited.
*/
void CAMainWin::onTimeEditedTimerTimeout() {
	_timeEditedTime++;
}

/*!
	Called when playback is finished or interrupted by the user.
	It stops the playback, closes ports etc.
*/
void CAMainWin::playbackFinished() {
	delete _playback;
	_playback = 0;
	uiPlayFromSelection->setChecked(false);
	static_cast<CAScoreViewPort*>(_playbackViewPort)->setPlaying(false);
	
	_repaintTimer->stop();
	_repaintTimer->disconnect();	/// \todo crashes, if disconnected sometimes. -Matevz
	delete _repaintTimer;			/// \todo crashes, if deleted. -Matevz
	CACanorus::midiDevice()->closeOutputPort();
	
	static_cast<CAScoreViewPort*>(_playbackViewPort)->clearSelection();
	static_cast<CAScoreViewPort*>(_playbackViewPort)->addToSelection( _prePlaybackSelection );
	static_cast<CAScoreViewPort*>(_playbackViewPort)->unsetBorder();
	_prePlaybackSelection.clear();
	
	setMode( mode() );
}

/*!
	Connected with the play button which starts the playback.
*/
void CAMainWin::on_uiPlayFromSelection_toggled(bool checked) {
	if (checked && currentScoreViewPort()) {
		CACanorus::midiDevice()->openOutputPort( CACanorus::settings()->midiOutPort() );
		_repaintTimer = new QTimer();
		_repaintTimer->setInterval(100);
		_repaintTimer->start();
		//connect(_repaintTimer, SIGNAL(timeout()), this, SLOT(on_repaintTimer_timeout())); //TODO: timeout is connected directly to repaint() directly. This should be optimized in the future -Matevz
		connect( _repaintTimer, SIGNAL(timeout()), this, SLOT( onRepaintTimerTimeout() ) );
		
		_playback = new CAPlayback(currentSheet(), CACanorus::midiDevice());
		if ( currentScoreViewPort()->selection().size() && currentScoreViewPort()->selection().at(0)->musElement() )
			_playback->setInitTimeStart( currentScoreViewPort()->selection().at(0)->musElement()->timeStart() );
		
		connect(_playback, SIGNAL(playbackFinished()), this, SLOT(playbackFinished()));
		
		QPen p;
		p.setColor(Qt::green);
		p.setWidth(3);
		
		_playbackViewPort = currentViewPort();
		currentScoreViewPort()->setBorder(p);
		currentScoreViewPort()->setPlaying(true);	// set the deadlock for borders
		
		// Remember old selection
		_prePlaybackSelection = currentScoreViewPort()->selection();
		currentScoreViewPort()->clearSelection();
		
		_playback->start();
	} else if(_playback) {
		_playback->stop();
	}
}

/*!
	Called every few miliseconds during playback to repaint score viewport as the GUI can
	only be repainted from the main thread.
*/
void CAMainWin::onRepaintTimerTimeout() {
	CAScoreViewPort *sv = static_cast<CAScoreViewPort*>(_playbackViewPort);
	sv->clearSelection();
	for (int i=0; i<_playback->curPlaying().size(); i++)
		sv->addToSelection( static_cast<CAMusElement*>(_playback->curPlaying().at(i)) );
	sv->repaint();
}

void CAMainWin::on_uiAnimatedScroll_toggled(bool val) {
	_animatedScroll = val;
}

void CAMainWin::on_uiLockScrollPlayback_toggled(bool val) {
	_lockScrollPlayback = val;
}

void CAMainWin::on_uiZoomToSelection_triggered() {
	if (_currentViewPort->viewPortType() == CAViewPort::ScoreViewPort)
		((CAScoreViewPort*)_currentViewPort)->zoomToSelection(_animatedScroll);
}

void CAMainWin::on_uiZoomToFit_triggered() {
	if (_currentViewPort->viewPortType() == CAViewPort::ScoreViewPort)
		((CAScoreViewPort*)_currentViewPort)->zoomToFit();
}

void CAMainWin::on_uiZoomToWidth_triggered() {
	if (_currentViewPort->viewPortType() == CAViewPort::ScoreViewPort)
		((CAScoreViewPort*)_currentViewPort)->zoomToWidth();
}

void CAMainWin::on_uiZoomToHeight_triggered() {
	if (_currentViewPort->viewPortType() == CAViewPort::ScoreViewPort)
		((CAScoreViewPort*)_currentViewPort)->zoomToHeight();
}

void CAMainWin::closeEvent(QCloseEvent *event) {	//TODO: Make the main window the main window of the application somehow - when it's closed, the destructor is also called. This way, this function will not be needed anymore. -Matevz
	clearUI();
}

void CAMainWin::on_uiOpenDocument_triggered() {
	if ( CAMainWin::uiOpenDialog->exec() && CAMainWin::uiOpenDialog->selectedFiles().size() ) {
		openDocument(CAMainWin::uiOpenDialog->selectedFiles().at(0));
	}
}

void CAMainWin::on_uiSaveDocument_triggered() {
	QString s;
	if (document()) {
		if ((s=document()->fileName()).isEmpty())
			on_uiSaveDocumentAs_triggered();
		else
			saveDocument(s);
	}
}

void CAMainWin::on_uiSaveDocumentAs_triggered() {
	if ( document() &&
	     CAMainWin::uiSaveDialog->exec() && CAMainWin::uiSaveDialog->selectedFiles().size()
	   ) {
		QString s = CAMainWin::uiSaveDialog->selectedFiles().at(0);
		// append the extension, if the filename doesn't contain a dot
		int i;
		if (!s.contains('.')) {
			int left = uiSaveDialog->selectedFilter().indexOf("(*.") + 2;
			int len = uiSaveDialog->selectedFilter().size() - left - 1;
			s.append( uiSaveDialog->selectedFilter().mid( left, len ) );
		}
		
		saveDocument(s);
	}
}

/*!
	Opens a document with the given absolute file name.
	The previous document will be lost.
	
	Returns a pointer to the opened document or null if opening the document has failed.
*/
CADocument *CAMainWin::openDocument(const QString& fileName) {
	stopPlayback();
	
	CAImport *open;
	if ( fileName.endsWith(".xml") ) {
		open = new CACanorusMLImport();
	} else if ( fileName.endsWith(".can") ) {
		open = new CACanImport();
	}
	
	open->setStreamFromFile( fileName );
	open->importDocument();
	open->wait();
	
	if( open->importedDocument() ) {
		CADocument *doc = open->importedDocument();
		delete open;
		
		doc->setFileName(fileName);
		return openDocument( doc );
	} else {
		QMessageBox::critical(
			this, tr("Canorus"),
			tr("Error while opening the file!\nError %1: ").arg(open->status()) + open->readableStatus()
		);
		delete open;
		return 0;
	}
}

/*!
	Opens the given document.
	The previous document will be lost.
	
	Returns a pointer to the opened document or null if opening the document has failed.
*/
CADocument *CAMainWin::openDocument(CADocument *doc) {
	stopPlayback();
	if (doc) {
		if ( document() && CACanorus::mainWinCount(document())==1 ) {
			CACanorus::undo()->deleteUndoStack( document() ); 
			delete document();
		}
		
		setDocument(doc);
		if ( !doc->fileName().isEmpty() )
			CACanorus::insertRecentDocument( doc->fileName() );
		CACanorus::undo()->createUndoStack( document() );
		
		rebuildUI(); // local rebuild only
		if ( doc->sheetCount())
			uiTabWidget->setCurrentIndex(0);
		
		// select the first context automatically
		if ( document() && document()->sheetCount() && document()->sheetAt(0)->contextCount() )
			currentScoreViewPort()->selectContext( document()->sheetAt(0)->contextAt(0) );
		updateToolBars();
		
		return doc;
	}
	
	return 0;
}

/*!
	Saves the current document to a given absolute \a fileName.
	This function is usually called when the filename was entered and save document dialog
	successfully closed.
	If \a recovery is False (default), it changes the timeEdit, fileName and some other document
	properties before saving it.
	
	Returns True, if the save was complete; False otherwise.
*/
bool CAMainWin::saveDocument( QString fileName ) {
	document()->setTimeEdited( document()->timeEdited() + _timeEditedTime );
	document()->setDateLastModified( QDateTime::currentDateTime() );
	CACanorus::restartTimeEditedTimes( document() );
	
	CAExport *save=0;
	if ( uiSaveDialog->selectedFilter()==CAFileFormats::CANORUSML_FILTER ) {
		save = new CACanorusMLExport();
	} else if ( uiSaveDialog->selectedFilter()==CAFileFormats::CAN_FILTER ) {
		save = new CACanExport();
	}
	
	if (save) {
		save->setStreamToFile( fileName );
		save->exportDocument( document() );
		save->wait();
		
		if ( save->exportedDocument() ) {
			document()->setFileName( fileName );
			CACanorus::insertRecentDocument( fileName );
			delete save;
			return true;
		}
	}
	
	return false;
}

void CAMainWin::onMidiInEvent( QVector<unsigned char> m) {
	std::cout << "MidiInEvent: ";
	for (int i=0; i<m.size(); i++)
		std::cout << (int)m[i] << " ";
	std::cout << std::endl;
}

/*!
	Called when File->Export is clicked.
*/
void CAMainWin::on_uiExportDocument_triggered() {
	QStringList fileNames;
	QString fileExtString;
	QStringList fileExtList;
	int ffound = uiExportDialog->exec();
	if (ffound)
		fileNames = uiExportDialog->selectedFiles();
	
	if (!ffound)
		return;
	
	QString s = fileNames[0];
	if (!s.contains('.')) {
		int left = uiExportDialog->selectedFilter().indexOf("(*.") + 2;
		int len = uiExportDialog->selectedFilter().size() - left - 1;
		fileExtString = uiExportDialog->selectedFilter().mid( left, len );
		// the default file extension is the first one:
		fileExtList = fileExtString.split( " " );
		s.append( fileExtList[0] );
	}

	if (CAPluginManager::exportFilterExists(uiExportDialog->selectedFilter())) {
		CAPluginManager::exportAction(uiExportDialog->selectedFilter(), document(), s);
	} else {
		if ( uiExportDialog->selectedFilter() == CAFileFormats::MIDI_FILTER ) {
			CAMidiExport me;
			me.setStreamToFile( s );
			me.exportDocument( document() );
			me.wait();
		} else {
			CALilyPondExport le;
			le.setStreamToFile( s );
			le.exportDocument( document() );
			le.wait();
		}
	}
}

/*!
	Called when File->Import is clicked.
*/
void CAMainWin::on_uiImportDocument_triggered() {
	QStringList fileNames;
	int ffound = uiImportDialog->exec();
	if (ffound)
		fileNames = uiImportDialog->selectedFiles();
	
	if (!ffound)
		return;
	stopPlayback();
	
	QString s = fileNames[0];
	CADocument *oldDocument = document();
	
	bool success=false;
	if (CAPluginManager::importFilterExists(uiImportDialog->selectedFilter())) {
		setDocument(new CADocument());
		CACanorus::undo()->createUndoStack( document() );
		
		CAPluginManager::importAction(uiImportDialog->selectedFilter(), document(), fileNames[0]);
		
		success=true;
	} else {
		QFile file(s);
		if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
			QTextStream in(&file);
			/// \todo Call appropriate built-in import function here
			/// eg. CALilyImport::importDocument(in, document());
			file.close();
		}
	}
	
	// clear existing document
	if ( success && oldDocument && (CACanorus::mainWinCount(oldDocument) == 1) ) {
		CACanorus::undo()->deleteUndoStack( oldDocument ); 
		delete oldDocument;
	}
	
	// select the first context automatically
	if ( document() && document()->sheetCount() && document()->sheetAt(0)->contextCount() )
		currentScoreViewPort()->selectContext( document()->sheetAt(0)->contextAt(0) );
	
	updateToolBars();
}

/*!
	Called when a user changes the current voice number.
*/
void CAMainWin::on_uiVoiceNum_valChanged(int voiceNr) {
	updateVoiceToolBar();
	if ( currentScoreViewPort() ) {
		if ( voiceNr &&
		     currentScoreViewPort()->currentContext() &&
		     currentScoreViewPort()->currentContext()->context()->contextType() == CAContext::Staff
		   )
			currentScoreViewPort()->setSelectedVoice( static_cast<CAStaff*>(currentScoreViewPort()->currentContext()->context())->voiceAt(voiceNr-1) );
		else
			currentScoreViewPort()->setSelectedVoice(0);
		
		currentScoreViewPort()->repaint();
	}
}

/*!
	Brings up the properties dialog.
*/
void CAMainWin::on_uiVoiceProperties_triggered() {
	CAPropertiesDialog::voiceProperties( currentVoice(), this );
}

/*!
	Changes the number of accidentals.
*/
void CAMainWin::on_uiKeySig_activated( int row ) {
	signed char accs = qRound((row-14.5) / 2);
	CAKeySignature::CAMajorMinorGender gender = (row%2)==0 ? CAKeySignature::Major : CAKeySignature::Minor;
	
	if (mode()==InsertMode) {
		musElementFactory()->setKeySigNumberOfAccs( accs );
		musElementFactory()->setKeySigGender( gender );
	} else
	if ( mode()==EditMode && currentScoreViewPort() && currentScoreViewPort()->selection().size() ) {
		QList<CADrawableMusElement*> list = currentScoreViewPort()->selection();
		CACanorus::undo()->createUndoCommand( document(), tr("change key signature", "undo") );
		
		for ( int i=0; i<list.size(); i++ ) {
			CAKeySignature *keySig = dynamic_cast<CAKeySignature*>(list[i]->musElement());
			CAFunctionMarking *fm = dynamic_cast<CAFunctionMarking*>(list[i]->musElement());
			
			if ( keySig ) {
				keySig->setKeySignatureType( CAKeySignature::MajorMinor, accs, gender );
			}
			
			if ( fm ) {
				fm->setKey( CAKeySignature::keySignatureToString( accs, gender ) );
			}
		}
		
		CACanorus::undo()->pushUndoCommand();
		CACanorus::rebuildUI(document(), currentSheet());
	}
}

/*!
	Changes the offset of the clef.
*/
void CAMainWin::on_uiClefOffset_valueChanged( int newOffset ) {
	if ( oldUiClefOffsetValue==0 && qAbs(newOffset)==1 ) {
		uiClefOffset->setValue( (newOffset/qAbs(newOffset))*2 );
		return;
	} else
	if ( qAbs(oldUiClefOffsetValue)==2 && qAbs(newOffset)==1 ) {
		uiClefOffset->setValue( 0 );
		return;
	}
	
	oldUiClefOffsetValue=newOffset;
	if (mode()==InsertMode) {
		musElementFactory()->setClefOffset( newOffset );
	} else if ( mode()==EditMode ) {
		CAScoreViewPort *v = currentScoreViewPort();
		if ( v && v->selection().size() ) {
			CACanorus::undo()->createUndoCommand( document(), tr("change clef offset", "undo") );		
			CAClef *clef = dynamic_cast<CAClef*>(v->selection().at(0)->musElement());
			
			if ( clef ) {
				clef->setOffset( CAClef::offsetFromReadable(newOffset) );
				CACanorus::undo()->pushUndoCommand();
				CACanorus::rebuildUI(document(), currentSheet());
			}
		}
	}
}

/*!
	Gets the current voice and sets its name.
*/
void CAMainWin::on_uiVoiceName_returnPressed() {
	CAVoice *voice = currentVoice();
	if (voice) {
		CACanorus::undo()->createUndoCommand( document(), tr("change voice name", "undo") );
		CACanorus::undo()->pushUndoCommand();
		voice->setName(uiVoiceName->text());
	}
}

/*!
	Changes voice instrument.
*/
void CAMainWin::on_uiVoiceInstrument_activated( int index ) {
	if ( !currentVoice() || index < 0 )
		return;
	
	currentVoice()->setMidiProgram( (unsigned char)index );
}

void CAMainWin::on_uiInsertPlayable_toggled(bool checked) {
	if (checked) {
		if (!uiVoiceNum->getRealValue())
			uiVoiceNum->setRealValue( 1 ); // select the first voice if none selected
		
		musElementFactory()->setMusElementType( CAMusElement::Note );
		setMode( InsertMode );
	}
}

void CAMainWin::on_uiInsertSyllable_toggled( bool checked ) {
	if (checked) {
		setMode( InsertMode );
	}
}

void CAMainWin::on_uiInsertFM_toggled(bool checked) {
	if (checked) {
		musElementFactory()->setMusElementType( CAMusElement::FunctionMarking );
		setMode( InsertMode );
	}
}

void CAMainWin::on_uiPlayableLength_toggled(bool checked, int buttonId) {
	// Read currently selected entry from tool button menu
	CAPlayable::CAPlayableLength length =
		static_cast<CAPlayable::CAPlayableLength>(buttonId);
		
	if ( mode()==InsertMode ) {
		// New note length type
		musElementFactory()->setPlayableLength( length );
	} else
	if ( mode()==EditMode && currentScoreViewPort() && currentScoreViewPort()->selection().size()) {
		CAScoreViewPort *v = currentScoreViewPort();
		CACanorus::undo()->createUndoCommand( document(), tr("change playable length", "undo") );
		
		for ( int i=0; i<v->selection().size(); i++ ) {
			CAPlayable *p = dynamic_cast<CAPlayable*>( v->selection().at(i)->musElement() );
			
			if (p) {
				CAMusElement *next=0;
				int oldLength = p->timeLength();
				int newLength = CAPlayable::playableLengthToTimeLength( length );
				if ( p->musElementType()==CAMusElement::Note ) {                   // change the length of the whole chord
					QList<CANote*> chord = static_cast<CANote*>(p)->getChord();
					for (int i=0; i<chord.size(); i++) {
						next = p->voice()->next(p);
						p->voice()->remove(chord[i]);
						chord[i]->setPlayableLength( length );
					}
					p->voice()->insert( next, chord[0], false );
					for (int i=1; i<chord.size(); i++) {
						p->voice()->insert( chord[0], chord[i], true );
					}
				} else if ( p->musElementType()==CAMusElement::Rest ) {
					next = p->voice()->next(p);
					p->voice()->remove(p);
					p->setPlayableLength( length );
					p->voice()->insert( next, p );
				}
				
				if (newLength<oldLength) { // insert rests, if the new length is shorter to keep consistency
					QList<CARest*> rests = CARest::composeRests( oldLength - newLength, p->timeStart()+p->timeLength(), p->voice(), CARest::Normal );
					for (int i=rests.size()-1; i>=0; i--) {
						p->voice()->insert( next, rests[i] ); // insert rests from shortest to longest
					}
				} else {
					p->staff()->synchronizeVoices();
				}
				
				for (int j=0; j<p->voice()->lyricsContextList().size(); j++) { // reposit syllables
					p->voice()->lyricsContextList().at(j)->repositSyllables();
				}
			}
		}
		
		CACanorus::undo()->pushUndoCommand();
		CACanorus::rebuildUI( document(), currentSheet() );
	}

}

/*!
	Function called when user types the text of the syllable or hits control keys.
	
	The following behaviour is implemented:
		- alphanumeric keys are pressed - writes text
		- spacebar is pressed - creates the current syllable and jumps to the next syllable
		- return is pressed - creates the current syllable and hides the syllable edit widget
		- left key is pressed - if cursorPosition()==0, jumps to the previous syllable
		- right key is pressed - if cursorPosition()==length(), same as spacebar
		- escape key is pressed - hides the syllable edit and cancels any changes to syllable
	
*/
void CAMainWin::onTextEditKeyPressEvent(QKeyEvent *e) {
	if (!currentScoreViewPort()) return;
	CATextEdit *textEdit = static_cast<CATextEdit*>(sender());
	
	CAScoreViewPort *v = currentScoreViewPort();
	CADrawableContext *dContext = v->currentContext();
	CAMusElement *elt = (v->selection().size()?v->selection().front()->musElement():0);
	
	if ( elt->musElementType()==CAMusElement::Syllable ) {
		CASyllable *syllable = static_cast<CASyllable*>(elt);
		
		QString text = textEdit->text().simplified(); // remove any trailing whitespaces
		
		bool hyphen = false;
		if (text.right(1)=="-") { hyphen = true; text.chop(1); }
		
		bool melisma = false;
		if (text.right(1)=="_") { melisma = true; text.chop(1); }
		
		CAVoice *voice = 0; /// \todo GUI for syllable specific associated voice - current is the default lyrics context's one
		
		CALyricsContext *lc = static_cast<CALyricsContext*>(dContext->context());
		
		// create or edit syllable
		if ( e->key()==Qt::Key_Space  ||
		     e->key()==Qt::Key_Return ||
		     e->key()==Qt::Key_Right && textEdit->cursorPosition()==textEdit->text().size() ||
		     (e->key()==Qt::Key_Left || e->key()==Qt::Key_Backspace) && textEdit->cursorPosition()==0 || 
		     CACanorus::settings()->finaleLyricsBehaviour() && e->key()==Qt::Key_Minus
		) {
			CACanorus::undo()->createUndoCommand( document(), tr("lyrics edit", "undo") );
			syllable->setText(text);
			syllable->setHyphenStart(hyphen);
			syllable->setMelismaStart(melisma);
			
			v->removeTextEdit();
			
			CAVoice *voice = (syllable->associatedVoice()?syllable->associatedVoice():lc->associatedVoice());
			CAMusElement *nextSyllable = 0;
			if (syllable) {
				if (e->key()==Qt::Key_Space || e->key()==Qt::Key_Right || e->key()==Qt::Key_Return) { // next right note
					nextSyllable = syllable->lyricsContext()->next(syllable);
				} else  if (e->key()==Qt::Key_Left || e->key()==Qt::Key_Backspace) {                  // next left note
					nextSyllable = syllable->lyricsContext()->previous(syllable);
				} else if (e->key()==Qt::Key_Minus) {
					syllable->setHyphenStart(true);
					nextSyllable = syllable->lyricsContext()->next(syllable);
				}
				CACanorus::undo()->pushUndoCommand();
				CACanorus::rebuildUI( document(), currentSheet() );
				if (nextSyllable) {
					CADrawableMusElement *dNextSyllable = v->selectMElement(nextSyllable);
					v->createTextEdit( dNextSyllable );
					if ( e->key()==Qt::Key_Space || e->key()==Qt::Key_Right || e->key()==Qt::Key_Return )
						v->textEdit()->setCursorPosition(0); // go to the beginning if moving to the right next syllable
				}
			}
		}
	} else if (elt->musElementType()==CAMusElement::Mark && static_cast<CAMark*>(elt)->markType()==CAMark::Text || static_cast<CAMark*>(elt)->markType()==CAMark::BookMark) {
		if (e->key()==Qt::Key_Return) {
			CAMark *mark = static_cast<CAMark*>(elt);
			CACanorus::undo()->createUndoCommand( document(), tr("text edit", "undo") );
			if (mark->markType()==CAMark::Text) {
				static_cast<CAText*>(mark)->setText( textEdit->text() );
			} else if (mark->markType()==CAMark::BookMark) {
				static_cast<CABookMark*>(mark)->setText( textEdit->text() );
			}
			v->removeTextEdit();
			CACanorus::undo()->pushUndoCommand();
			CACanorus::rebuildUI( document(), currentSheet() );
		}
	} else {
		v->removeTextEdit();
	}
	
	// escape key - cancel
	if (e->key()==Qt::Key_Escape) {
		v->removeTextEdit();
	}
}

void CAMainWin::on_uiFMFunction_toggled( bool checked, int buttonId ) {
	if ( mode()==InsertMode ) {
		musElementFactory()->setFMFunction( static_cast<CAFunctionMarking::CAFunctionType>( buttonId * (buttonId<0?-1:1) ));
		musElementFactory()->setFMFunctionMinor( buttonId<0 );
	} else
	if ( mode()==EditMode && currentScoreViewPort() && currentScoreViewPort()->selection().size()) {
		CAScoreViewPort *v = currentScoreViewPort();
		CACanorus::undo()->createUndoCommand( document(), tr("change function", "undo") );
		
		for ( int i=0; i<v->selection().size(); i++ ) {
			CAFunctionMarking *fm = dynamic_cast<CAFunctionMarking*>( v->selection().at(i)->musElement() );
			
			if ( fm ) {
				fm->setFunction( static_cast<CAFunctionMarking::CAFunctionType>( buttonId * (buttonId<0?-1:1) ));
				fm->setMinor( buttonId<0 );
			}
		}
		
		CACanorus::undo()->pushUndoCommand();
		CACanorus::rebuildUI( document(), currentSheet() );
	}
}

void CAMainWin::on_uiFMChordArea_toggled(bool checked, int buttonId) {
	if ( mode()==InsertMode ) {
		musElementFactory()->setFMChordArea( static_cast<CAFunctionMarking::CAFunctionType>( buttonId * (buttonId<0?-1:1) ));
		musElementFactory()->setFMChordAreaMinor( buttonId<0 );
	} else
	if ( mode()==EditMode && currentScoreViewPort() && currentScoreViewPort()->selection().size()) {
		CAScoreViewPort *v = currentScoreViewPort();
		CACanorus::undo()->createUndoCommand( document(), tr("change chord area", "undo") );
		
		for ( int i=0; i<v->selection().size(); i++ ) {
			CAFunctionMarking *fm = dynamic_cast<CAFunctionMarking*>( v->selection().at(i)->musElement() );
			
			if ( fm ) {
				fm->setChordArea( static_cast<CAFunctionMarking::CAFunctionType>( buttonId * (buttonId<0?-1:1) ));
				fm->setChordAreaMinor( buttonId<0 );
			}
		}
		
		CACanorus::undo()->pushUndoCommand();
		CACanorus::rebuildUI( document(), currentSheet() );
	}
}

void CAMainWin::on_uiFMTonicDegree_toggled(bool checked, int buttonId) {
	if ( mode()==InsertMode ) {
		musElementFactory()->setFMTonicDegree( static_cast<CAFunctionMarking::CAFunctionType>( buttonId * (buttonId<0?-1:1) ));
		musElementFactory()->setFMTonicDegreeMinor( buttonId<0 );
	} else
	if ( mode()==EditMode && currentScoreViewPort() && currentScoreViewPort()->selection().size()) {
		CAScoreViewPort *v = currentScoreViewPort();
		CACanorus::undo()->createUndoCommand( document(), tr("change tonic degree", "undo") );
		
		for ( int i=0; i<v->selection().size(); i++ ) {
			CAFunctionMarking *fm = dynamic_cast<CAFunctionMarking*>( v->selection().at(i)->musElement() );
			
			if ( fm ) {
				fm->setTonicDegree( static_cast<CAFunctionMarking::CAFunctionType>( buttonId * (buttonId<0?-1:1) ));
				fm->setTonicDegreeMinor( buttonId<0 );
			}
		}
		
		CACanorus::undo()->pushUndoCommand();
		CACanorus::rebuildUI( document(), currentSheet() );
	}
}

void CAMainWin::on_uiFMEllipse_toggled( bool checked ) {
	if ( mode()==InsertMode ) {
		musElementFactory()->setFMEllipse( checked );
	} else
	if ( mode()==EditMode && currentScoreViewPort() && currentScoreViewPort()->selection().size()) {
		CAScoreViewPort *v = currentScoreViewPort();
		CACanorus::undo()->createUndoCommand( document(), tr("set/unset ellipse", "undo") );
		
		for ( int i=0; i<v->selection().size(); i++ ) {
			CAFunctionMarking *fm = dynamic_cast<CAFunctionMarking*>( v->selection().at(i)->musElement() );
			
			if ( fm ) {
				fm->setEllipse( checked );
			}
		}
		
		CACanorus::undo()->pushUndoCommand();
		CACanorus::rebuildUI( document(), currentSheet() );
	}
}

void CAMainWin::on_uiSlurType_toggled( bool checked, int buttonId ) {
	// remember previous muselement type so we can return to previous state after
	CAMusElement::CAMusElementType prevMusEltType =
		musElementFactory()->musElementType();
	
	// Read currently selected entry from tool button menu
	CASlur::CASlurType slurType =
		static_cast<CASlur::CASlurType>(buttonId);
	
	musElementFactory()->setMusElementType( CAMusElement::Slur );
	
	// New clef type
	musElementFactory()->setSlurType( slurType );
	
	insertMusElementAt( QPoint(0,0), currentScoreViewPort() ); // inserts a slur or tie and quits the insert mode
	
	musElementFactory()->setMusElementType( prevMusEltType );
}

void CAMainWin::on_uiClefType_toggled(bool checked, int buttonId) {
	if ( checked ) {
		// Read currently selected entry from tool button menu
		CAClef::CAPredefinedClefType clefType =
			static_cast<CAClef::CAPredefinedClefType>(buttonId);
			
		musElementFactory()->setMusElementType( CAMusElement::Clef );
		
		// New clef type
		musElementFactory()->setClef( clefType );
		
		setMode( InsertMode );
	}
}

void CAMainWin::on_uiMarkType_toggled( bool checked, int buttonId ) {
	if ( checked ) {
		CAMark::CAMarkType markType;
		
		// Read currently selected entry from tool button menu
		if ( buttonId==CAMark::Ritardando*(-1) ) {
			markType = CAMark::Ritardando;
			musElementFactory()->setRitardandoType( CARitardando::Accellerando );
		} else
		if ( buttonId==CAMark::Ritardando ) {
			markType = CAMark::Ritardando;
			musElementFactory()->setRitardandoType( CARitardando::Ritardando );
		} else 
		if ( buttonId==CAMark::Crescendo*(-1) ) {
			markType = CAMark::Crescendo;
			musElementFactory()->setCrescendoType( CACrescendo::Decrescendo );
		} else
		if ( buttonId==CAMark::Crescendo ) {
			markType = CAMark::Crescendo;
			musElementFactory()->setCrescendoType( CACrescendo::Crescendo );
		} else {
			markType = static_cast<CAMark::CAMarkType>(buttonId);
		}
		
		musElementFactory()->setMusElementType( CAMusElement::Mark );
		
		// New mark type
		musElementFactory()->setMarkType( markType );
		
		setMode( InsertMode );
	}
}

void CAMainWin::on_uiArticulationType_toggled(bool checked, int buttonId) {
	if ( checked ) {
		// Read currently selected entry from tool button menu
		CAArticulation::CAArticulationType type =
			static_cast<CAArticulation::CAArticulationType>(buttonId);
			
		musElementFactory()->setMusElementType( CAMusElement::Mark );
		
		// New mark type
		musElementFactory()->setMarkType( CAMark::Articulation );
		musElementFactory()->setArticulationType( type );
		
		setMode( InsertMode );
	}
}

void CAMainWin::on_uiTimeSigBeats_valueChanged(int beats) {
	if (mode()==InsertMode) {
		musElementFactory()->setTimeSigBeats( beats );
		if (uiTimeSigBeats->value()==4 && uiTimeSigBeat->value()==4)
			uiTimeSigType->setCurrentId( 44 );
		else if (uiTimeSigBeats->value()==3 && uiTimeSigBeat->value()==4)
			uiTimeSigType->setCurrentId( 34 );
		else if (uiTimeSigBeats->value()==2 && uiTimeSigBeat->value()==4)
			uiTimeSigType->setCurrentId( 24 );
		else if (uiTimeSigBeats->value()==2 && uiTimeSigBeat->value()==2)
			uiTimeSigType->setCurrentId( 22 );
		else if (uiTimeSigBeats->value()==3 && uiTimeSigBeat->value()==8)
			uiTimeSigType->setCurrentId( 38 );
		else if (uiTimeSigBeats->value()==6 && uiTimeSigBeat->value()==8)
			uiTimeSigType->setCurrentId( 68 );
		else
			uiTimeSigType->setCurrentId( 0 );
	} else if ( mode()==EditMode ) {
		CAScoreViewPort *v = currentScoreViewPort();
		if ( v && v->selection().size() ) {
			CATimeSignature *timeSig = dynamic_cast<CATimeSignature*>(v->selection().at(0)->musElement());
			if ( timeSig ) {
				timeSig->setBeats( beats );
				CACanorus::rebuildUI(document(), currentSheet());
			}
		}
	}
}

void CAMainWin::on_uiTimeSigBeat_valueChanged(int beat) {
	if (mode()==InsertMode) {
		musElementFactory()->setTimeSigBeat( beat );
		if (uiTimeSigBeats->value()==4 && uiTimeSigBeat->value()==4)
			uiTimeSigType->setCurrentId( 44 );
		else if (uiTimeSigBeats->value()==3 && uiTimeSigBeat->value()==4)
			uiTimeSigType->setCurrentId( 34 );
		else if (uiTimeSigBeats->value()==2 && uiTimeSigBeat->value()==4)
			uiTimeSigType->setCurrentId( 24 );
		else if (uiTimeSigBeats->value()==2 && uiTimeSigBeat->value()==2)
			uiTimeSigType->setCurrentId( 22 );
		else if (uiTimeSigBeats->value()==3 && uiTimeSigBeat->value()==8)
			uiTimeSigType->setCurrentId( 38 );
		else if (uiTimeSigBeats->value()==6 && uiTimeSigBeat->value()==8)
			uiTimeSigType->setCurrentId( 68 );
		else
			uiTimeSigType->setCurrentId( 0 );
	} else if ( mode()==EditMode ) {
		CAScoreViewPort *v = currentScoreViewPort();
		if ( v && v->selection().size() ) {
			CATimeSignature *timeSig = dynamic_cast<CATimeSignature*>(v->selection().at(0)->musElement());
			if ( timeSig ) {
				timeSig->setBeat( beat );
				CACanorus::rebuildUI(document(), currentSheet());
			}
		}
	}
}

void CAMainWin::on_uiTimeSigType_toggled(bool checked, int buttonId) {
	if (checked) {
		switch( buttonId ) {
			case 44:
			  uiTimeSigBeats->setValue( 4 );
			  uiTimeSigBeat->setValue( 4 );
			  break;
			case 22:
			  uiTimeSigBeats->setValue( 2 );
			  uiTimeSigBeat->setValue( 2 );
			  break;
			case 34:
			  uiTimeSigBeats->setValue( 3 );
			  uiTimeSigBeat->setValue( 4 );
			  break;
			case 24:
			  uiTimeSigBeats->setValue( 2 );
			  uiTimeSigBeat->setValue( 4 );
			  break;
			case 38:
			  uiTimeSigBeats->setValue( 3 );
			  uiTimeSigBeat->setValue( 8 );
			  break;
			case 68:
			  uiTimeSigBeats->setValue( 6 );
			  uiTimeSigBeat->setValue( 8 );
			  break;
			case 0:
			  break;
		}
		musElementFactory()->setTimeSigBeats( uiTimeSigBeats->value() );
		musElementFactory()->setTimeSigBeat( uiTimeSigBeat->value() );
		musElementFactory()->setMusElementType( CAMusElement::TimeSignature );
		
		setMode( InsertMode );
	}
}

void CAMainWin::on_uiInsertKeySig_toggled(bool checked) {
	if (checked) {
		musElementFactory()->setMusElementType( CAMusElement::KeySignature );
		setMode( InsertMode );
	}
}

void CAMainWin::on_uiBarlineType_toggled(bool checked, int buttonId) {
	if (checked) {
		musElementFactory()->setMusElementType( CAMusElement::Barline );
		musElementFactory()->setBarlineType( static_cast<CABarline::CABarlineType>(buttonId) );
		setMode( InsertMode );
	}
}

/*!
	Called when a user clicks "Commit" button in source viewport.
*/
void CAMainWin::sourceViewPortCommit(QString inputString, CASourceViewPort *v) {
	stopPlayback();
	if (v->document()) {
		// CanorusML document source
		CACanorus::undo()->createUndoCommand( document(), tr("commit CanorusML source", "undo") );
		
		clearUI(); 	// clear GUI before clearing the data part!
		if ( document() )
			delete document();
		
		CACanorus::undo()->pushUndoCommand();
		CACanorusMLImport open( inputString );
		open.importDocument();
		open.wait();
		
		if (open.importedDocument()) {
			CACanorus::undo()->changeDocument( document(), open.importedDocument() );
			setDocument( open.importedDocument() );
		}
		CACanorus::rebuildUI(document());
	} else
	if (v->voice()) {
		// LilyPond voice source
		CACanorus::undo()->createUndoCommand( document(), tr("commit LilyPond source", "undo") );
		
		CALilyPondImport li( inputString );
		
		CAVoice *oldVoice = v->voice();
		
		QList<CAMusElement*> signList = oldVoice->getSignList();
		for (int i=0; i<signList.size(); i++)
			oldVoice->remove( signList[i] );  // removes signs from all voices
		
		li.setTemplateVoice( oldVoice );      // copy properties
		li.importVoice();
		li.wait();
		
		CAVoice *newVoice = li.importedVoice();
		delete oldVoice; // also removes voice from the staff
		
		newVoice->staff()->addVoice( newVoice );
		newVoice->staff()->synchronizeVoices();
		
		// FIXME any way to avoid this?
		CAScoreViewPort *scorevp;
		foreach(CAViewPort* vp, _viewPortList) {
			if(vp->viewPortType() == CAViewPort::ScoreViewPort && (scorevp = static_cast<CAScoreViewPort*>(vp))->selectedVoice() == oldVoice)
				scorevp->setSelectedVoice(newVoice);
		}
		
		v->setVoice( newVoice );
		CACanorus::undo()->pushUndoCommand();
		CACanorus::rebuildUI(document(), newVoice->staff()->sheet());
	} else
	if (v->lyricsContext()) {
		// LilyPond lyrics source
		CACanorus::undo()->createUndoCommand( document(), tr("commit LilyPond source", "undo") );
		
		CALilyPondImport li( inputString );
		
		li.importLyricsContext();
		li.wait();
		CALyricsContext *newLc = li.importedLyricsContext();
		CALyricsContext *oldLc = v->lyricsContext();
		newLc->cloneLyricsContextProperties( oldLc );
		if (newLc->associatedVoice()) {
			newLc->associatedVoice()->removeLyricsContext( oldLc );
			newLc->associatedVoice()->addLyricsContext(newLc);
		}
		newLc->sheet()->insertContextAfter( oldLc, newLc );
		newLc->sheet()->removeContext( oldLc );
		v->setLyricsContext( newLc );
		delete oldLc;
		
		CACanorus::undo()->pushUndoCommand();
		CACanorus::rebuildUI(document(), v->lyricsContext()->sheet());
	}
	
	setCurrentViewPort( v );
}

void CAMainWin::on_uiAboutQt_triggered() {
	QMessageBox::aboutQt( this, tr("About Qt") );
}

void CAMainWin::on_uiAboutCanorus_triggered() {
	QMessageBox::about( this, tr("About Canorus"),
	tr("Canorus - The next generation music score editor\n\n\
Version %1\n\
(C) 2006-2008 Canorus Development team. All rights reserved.\n\
See the file AUTHORS for the list of Canorus developers\n\n\
This program is licensed under the GNU General Public License (GPL).\n\
See the file LICENSE.GPL for details.\n\n\
Homepage: http://www.canorus.org").arg(CANORUS_VERSION) );
}

void CAMainWin::on_uiSettings_triggered() {
	CASettingsDialog( CASettingsDialog::EditorSettings, this );
	CACanorus::rebuildUI();
}

void CAMainWin::on_uiLilyPondSource_triggered() {
	CAContext *context = currentContext();
	if ( !context ) 
		return;
	
	CASourceViewPort *v=0;
	CAStaff *staff = 0;
	if (staff = currentStaff()) {
		int voiceNum = uiVoiceNum->getRealValue()-1<0?0:uiVoiceNum->getRealValue()-1;
		CAVoice *voice = staff->voiceAt( voiceNum );
		v = new CASourceViewPort(voice, 0);
	} else
	if (context->contextType()==CAContext::LyricsContext) {
		v = new CASourceViewPort(static_cast<CALyricsContext*>(context), 0);
	}
	
	initViewPort( v );
	currentViewPortContainer()->addViewPort( v );
	
	uiUnsplitAll->setEnabled(true);
	uiCloseCurrentView->setEnabled(true);
}

/*!
	Adds a new score viewport to default viewport container.
*/
void CAMainWin::on_uiScoreView_triggered() {
	CASheet* s = _sheetMap[currentViewPortContainer()];
	
	if ( currentViewPortContainer() && s ) {
		CAScoreViewPort *v = new CAScoreViewPort(s, 0);
		initViewPort( v );
		currentViewPortContainer()->addViewPort( v );
		v->rebuild();
		
		uiUnsplitAll->setEnabled(true);
		uiCloseCurrentView->setEnabled(true);
	}
}

/*!
	Removes the sheet, all its contents and rebuilds the GUI.
*/
void CAMainWin::on_uiRemoveSheet_triggered() {
	stopPlayback();
	CASheet *sheet = currentSheet();
	if (sheet) {
		CACanorus::undo()->createUndoCommand( document(), tr("deletion of the sheet", "undo") );
		CACanorus::undo()->pushUndoCommand();
		document()->removeSheet(currentSheet());
		removeSheet(sheet);
		delete sheet;
	}
}

/*!
	Removes the sheet from the GUI and deletes the viewports and viewport containers and tabs
	pointing to the given \a sheet.
*/
void CAMainWin::removeSheet( CASheet *sheet ) {
	CAViewPortContainer *vpc = _sheetMap.key(sheet);
	_sheetMap.remove(vpc);
	
	// remove tab
	int idx = uiTabWidget->indexOf(vpc);
	uiTabWidget->removeTab( idx );
	delete vpc;
	if (idx < uiTabWidget->count())
		uiTabWidget->setCurrentIndex(idx);
	setCurrentViewPortContainer( static_cast<CAViewPortContainer*>(uiTabWidget->currentWidget()) );
	setCurrentViewPort( static_cast<CAViewPortContainer*>(uiTabWidget->currentWidget())?static_cast<CAViewPortContainer*>(uiTabWidget->currentWidget())->currentViewPort():0 );
	
	// remove other viewports pointing to the sheet
	QList<CAViewPort*> vpl = viewPortList();
	for (int i=0; i<vpl.size(); i++) {
		switch (vpl[i]->viewPortType()) {
			case CAViewPort::ScoreViewPort: {
				if (static_cast<CAScoreViewPort*>(vpl[i])->sheet()==sheet)
					delete vpl[i];
				break;
			}
			case CAViewPort::SourceViewPort: {
				CASourceViewPort *sv = static_cast<CASourceViewPort*>(vpl[i]);
				if (sv->voice() && sv->voice()->staff()->sheet()==sheet)
					delete vpl[i];
				else
				if (sv->lyricsContext() && sv->lyricsContext()->sheet()==sheet)
					delete vpl[i];
				
				break;
			}
		}
	}
}

/*!
	Brings up the properties dialog.
*/
void CAMainWin::on_uiDocumentProperties_triggered() {
	CAPropertiesDialog::documentProperties( document(), this );
}

void CAMainWin::on_uiSheetName_returnPressed() {
	CASheet *sheet = currentSheet();
	if (sheet) {
		CACanorus::undo()->createUndoCommand( document(), tr("change sheet name", "undo") );
		CACanorus::undo()->pushUndoCommand();
		updateUndoRedoButtons();
		sheet->setName( uiSheetName->text() );
		uiTabWidget->setTabText(uiTabWidget->currentIndex(), sheet->name());
	}
}

/*!
	Brings up properties dialog.
*/
void CAMainWin::on_uiSheetProperties_triggered() {
	CAPropertiesDialog::sheetProperties( currentSheet(), this );
}

/*!
	Sets the current context name.
*/
void CAMainWin::on_uiContextName_returnPressed() {
	CAContext *context = currentContext();
	if (context) {
		CACanorus::undo()->createUndoCommand( document(), tr("change context name", "undo") );
		CACanorus::undo()->pushUndoCommand();
		updateUndoRedoButtons();
		context->setName(uiContextName->text());
	}
}

/*!
	Brings up the properties dialog.
*/
void CAMainWin::on_uiContextProperties_triggered() {
	CAPropertiesDialog::contextProperties( currentContext(), this );
}

/*!
	Sets the stanza number of the current lyrics context.
*/
void CAMainWin::on_uiStanzaNumber_valueChanged(int stanzaNumber) {
	if (currentContext() && currentContext()->contextType()==CAContext::LyricsContext) {
		CACanorus::undo()->createUndoCommand( document(), tr("change stanza number", "undo") );
		if (static_cast<CALyricsContext*>(currentContext())->stanzaNumber()!=stanzaNumber)
			CACanorus::undo()->pushUndoCommand();
		static_cast<CALyricsContext*>(currentContext())->setStanzaNumber( stanzaNumber );
	}
}

/*!
	Sets the associated voice of the current lyrics context.
*/
void CAMainWin::on_uiAssociatedVoice_activated(int idx) {
	if (idx != -1 && currentContext() && currentContext()->contextType()==CAContext::LyricsContext) {
		CACanorus::undo()->createUndoCommand( document(), tr("change associated voice", "undo") );
		if (static_cast<CALyricsContext*>(currentContext())->associatedVoice()!=currentSheet()->voiceList().at( idx ))
			CACanorus::undo()->pushUndoCommand();
		static_cast<CALyricsContext*>(currentContext())->setAssociatedVoice( currentSheet()->voiceList().at( idx ) );
		CACanorus::rebuildUI( document(), currentSheet() ); // needs a rebuild if lyrics contexts are to be moved
	}
}

void CAMainWin::on_uiVoiceStemDirection_toggled(bool checked, int direction) {
	CAVoice *voice = currentVoice();
	if (voice) {
		CACanorus::undo()->createUndoCommand( document(), tr("change voice stem direction", "undo") );
		if (voice->stemDirection()!=static_cast<CANote::CAStemDirection>(direction))
			CACanorus::undo()->pushUndoCommand();
		CACanorus::undo()->pushUndoCommand();
		voice->setStemDirection(static_cast<CANote::CAStemDirection>(direction));
		CACanorus::rebuildUI(document(), currentSheet());
	}
}

/*!
	Sets the currently selected note stem direction if in insert/edit mode or the music elements factory note stem direction if in insert mode.
*/
void CAMainWin::on_uiNoteStemDirection_toggled(bool checked, int id) {
	CANote::CAStemDirection direction = static_cast<CANote::CAStemDirection>(id);
	if (mode()==InsertMode)
		musElementFactory()->setNoteStemDirection( direction );
	else if (mode()==SelectMode || mode()==EditMode) {
		CACanorus::undo()->createUndoCommand( document(), tr("change note stem direction", "undo") );
		CAScoreViewPort *v = currentScoreViewPort();
		bool changed=false;
		for (int i=0; v && i<v->selection().size(); i++) {
			CANote *note = dynamic_cast<CANote*>(v->selection().at(i)->musElement());
			if ( note ) {
				note->setStemDirection( direction );
				changed=true;
			}
		}
		if (changed) {
			CACanorus::undo()->pushUndoCommand();
			CACanorus::rebuildUI(document(), currentSheet());
		}
	}
}

/*!
	Updates all the toolbars according to the current state of the main window.
*/
void CAMainWin::updateToolBars() {
	updateUndoRedoButtons();
	
	updateInsertToolBar();
	updateSheetToolBar();
	updateContextToolBar();
	updateVoiceToolBar();
	updatePlayableToolBar();
	updateKeySigToolBar();
	updateTimeSigToolBar();
	updateClefToolBar();
	updateFMToolBar();
	updateDynamicToolBar();
	updateInstrumentToolBar();
	updateTempoToolBar();
	updateFermataToolBar();
	updateRepeatMarkToolBar();
	updateFingeringToolBar();
	
	if ( document() )
		uiNewSheet->setVisible( true );
	else
		uiNewSheet->setVisible( false );
}

/*!
	Shows sheet tool bar if nothing selected. Otherwise hides it.
*/
void CAMainWin::updateSheetToolBar() {
	CAScoreViewPort *v = currentScoreViewPort();
	if (v && v->selection().isEmpty() && (!v->currentContext())) {
		if (v->sheet())
			uiSheetName->setText(v->sheet()->name());
		uiSheetToolBar->show();
	} else
		uiSheetToolBar->hide();
}

/*!
	Shows/Hides the Voice properties tool bar according to the currently selected context and updates its properties.
*/
void CAMainWin::updateVoiceToolBar() {
	CAContext *context = currentContext();
	if ( mode()==SelectMode && context && context->contextType() == CAContext::Staff ) {
		CAStaff *staff = static_cast<CAStaff*>(context);
		uiNewVoice->setVisible(true);
		uiNewVoice->setEnabled(true);
		if (staff->voiceCount()) {
			uiVoiceNum->setMax(staff->voiceCount());
			int voiceNr = uiVoiceNum->getRealValue();
			if (voiceNr) {
				CAVoice *curVoice = (voiceNr<=staff->voiceCount()?staff->voiceAt(voiceNr-1):staff->voiceAt(0));
				uiVoiceName->setText(curVoice->name());
				uiVoiceName->setEnabled(true);
				uiVoiceInstrument->setEnabled(true);
					uiVoiceInstrument->setCurrentIndex( currentVoice()->midiProgram() );
				uiRemoveVoice->setEnabled(true);
				uiVoiceStemDirection->setCurrentId( curVoice->stemDirection() );
				uiVoiceStemDirection->setEnabled(true);
				uiVoiceProperties->setEnabled(true);
			} else {
				uiVoiceName->setEnabled(false);
				uiVoiceInstrument->setEnabled(false);
				uiRemoveVoice->setEnabled(false);
				uiVoiceStemDirection->setEnabled(false);
				uiVoiceProperties->setEnabled(false);
			}
		}
		
		uiVoiceToolBar->show();
	} else {
		uiNewVoice->setVisible(false);
		uiVoiceToolBar->hide();
	}
}

/*!
	Shows/Hides context tool bar according to the selected context (if any) and hides/shows specific actions in the toolbar for the current context.
*/
void CAMainWin::updateContextToolBar() {
	CAContext *context = currentContext();
	if (mode()==SelectMode && context) {
		if (!uiInsertPlayable->isChecked())
			uiContextToolBar->show();
		
		switch (context->contextType()) {
			case CAContext::Staff: {
				uiStanzaNumberAction->setVisible(false);
				uiAssociatedVoiceAction->setVisible(false);
				break;
			}
			case CAContext::LyricsContext: {
				CALyricsContext *c = static_cast<CALyricsContext*>(context);				
				uiStanzaNumber->setValue(c->stanzaNumber());
				uiStanzaNumberAction->setVisible(true);
				
				uiAssociatedVoice->clear();
				QList<CAVoice*> voiceList = currentSheet()->voiceList();
				for (int i=0; i<voiceList.count(); i++) uiAssociatedVoice->addItem(voiceList[i]->name());
				uiAssociatedVoice->setCurrentIndex( voiceList.indexOf( c->associatedVoice() ) );
				uiAssociatedVoiceAction->setVisible(true);
				
				break;
			}
			case CAContext::FunctionMarkingContext: {
				uiStanzaNumberAction->setVisible(false);
				uiAssociatedVoiceAction->setVisible(false);
				break;
			}
		}
		uiContextName->setText(context->name());
	} else
		uiContextToolBar->hide();
}

/*!
	Shows/Hides music elements which cannot be placed in the selected context.
*/
void CAMainWin::updateInsertToolBar() {
	if ( currentSheet() ) {
		uiNewContext->setVisible(true);
		if (mode()==EditMode) {
			uiInsertToolBar->show();
		} else {
			uiInsertToolBar->show();
			CAContext *context = currentContext();
			if (context) {
				switch (context->contextType()) {
					case CAContext::Staff:
						// staff selected
						uiInsertPlayable->setVisible(true);
						uiSlurType->defaultAction()->setVisible(true);
						uiSlurType->setVisible(true); // \todo This is needed in order for actions to hide?! -Matevz
						uiInsertClef->setVisible(true); // menu
						uiInsertBarline->setVisible(true); // menu
						uiClefType->defaultAction()->setVisible(true);
						uiTimeSigType->defaultAction()->setVisible(true);
						uiInsertKeySig->setVisible(true);
						uiMarkType->defaultAction()->setVisible(true);
						uiArticulationType->defaultAction()->setVisible(true);
						uiInsertTimeSig->setVisible(true);
						uiBarlineType->defaultAction()->setVisible(true);
						uiInsertFM->setVisible(false);
						uiInsertSyllable->setVisible(false);
						break;
					case CAContext::FunctionMarkingContext:
						// function marking context selected
						uiInsertPlayable->setVisible(false);
						uiSlurType->defaultAction()->setVisible(false);
						uiInsertClef->setVisible(false); // menu
						uiInsertBarline->setVisible(false); // menu
						uiClefType->defaultAction()->setVisible(false);
						uiTimeSigType->defaultAction()->setVisible(false);
						uiInsertKeySig->setVisible(false);
						uiMarkType->defaultAction()->setVisible(false);
						uiArticulationType->defaultAction()->setVisible(false);
						uiInsertTimeSig->setVisible(false);
						uiBarlineType->defaultAction()->setVisible(false);
						uiInsertFM->setVisible(true);
						uiInsertSyllable->setVisible(false);
						break;
					case CAContext::LyricsContext:
						// lyrics context selected
						uiInsertPlayable->setVisible(false);
						uiSlurType->defaultAction()->setVisible(false);
						uiInsertClef->setVisible(false); // menu
						uiInsertBarline->setVisible(false); // menu
						uiClefType->defaultAction()->setVisible(false);
						uiTimeSigType->defaultAction()->setVisible(false);
						uiInsertKeySig->setVisible(false);
						uiMarkType->defaultAction()->setVisible(false);
						uiArticulationType->defaultAction()->setVisible(false);
						uiInsertTimeSig->setVisible(false);
						uiBarlineType->defaultAction()->setVisible(false);
						uiInsertFM->setVisible(false);
						uiInsertSyllable->setVisible(true);
						break;
				}
			} else {
				// no contexts selected
				uiInsertPlayable->setVisible(false);
				uiSlurType->defaultAction()->setVisible(false);
				uiSlurType->setVisible(false); // \todo This is needed in order for actions to hide?! -Matevz
				uiInsertClef->setVisible(false); // menu
				uiInsertBarline->setVisible(false); // menu
				uiClefType->defaultAction()->setVisible(false);
				uiTimeSigType->defaultAction()->setVisible(false);
				uiInsertKeySig->setVisible(false);
				uiMarkType->defaultAction()->setVisible(false);
				uiArticulationType->defaultAction()->setVisible(false);
				uiInsertTimeSig->setVisible(false);
				uiBarlineType->defaultAction()->setVisible(false);
				uiInsertFM->setVisible(false);
				uiInsertSyllable->setVisible(false);
			}
		}
	
	} else {
		uiInsertToolBar->hide();
		uiNewContext->setVisible(false);
		uiInsertPlayable->setVisible(false);
		uiSlurType->defaultAction()->setVisible(false);
		uiSlurType->setVisible(false); // \todo This is needed in order for actions to hide?! -Matevz
		uiInsertClef->setVisible(false); // menu
		uiInsertBarline->setVisible(false); // menu
		uiClefType->defaultAction()->setVisible(false);
		uiTimeSigType->defaultAction()->setVisible(false);
		uiInsertKeySig->setVisible(false);
		uiMarkType->defaultAction()->setVisible(false);
		uiArticulationType->defaultAction()->setVisible(false);
		uiInsertTimeSig->setVisible(false);
		uiBarlineType->defaultAction()->setVisible(false);
		uiInsertFM->setVisible(false);
		uiInsertSyllable->setVisible(false);
	}
}

/*!
	Show/Hides the playable tool bar and its properties according to the current state.
*/
void CAMainWin::updatePlayableToolBar() {
	if ( uiInsertPlayable->isChecked() && mode()==InsertMode ) {
		uiPlayableLength->setCurrentId( musElementFactory()->playableLength() );
		uiNoteStemDirection->setCurrentId( musElementFactory()->noteStemDirection() );
		uiHiddenRest->setEnabled(true);
		uiHiddenRest->setChecked( musElementFactory()->restType()==CARest::Hidden );
		uiPlayableToolBar->show();
	} else if (mode()==EditMode) {
		CAScoreViewPort *v = currentScoreViewPort();
		if (v && v->selection().size()) {
			CAPlayable *playable = dynamic_cast<CAPlayable*>(v->selection().at(0)->musElement());
			if (playable) {
				uiPlayableLength->setCurrentId( playable->playableLength() );
				if (playable->musElementType()==CAMusElement::Note) {
					CANote *note = static_cast<CANote*>(playable);
					uiNoteStemDirection->setCurrentId( note->stemDirection() );
					uiHiddenRest->setEnabled(false);
				} else if (playable->musElementType()==CAMusElement::Rest) {
					CARest *rest = static_cast<CARest*>(playable);
					uiHiddenRest->setEnabled(true);
					uiHiddenRest->setChecked(rest->restType()==CARest::Hidden);
				}
				uiPlayableToolBar->show();
				uiVoiceNum->setRealValue( playable->voice()->voiceNumber() );
			} else {
				uiPlayableToolBar->hide();
				uiHiddenRest->setEnabled(false);
			}
		}
	} else {
		uiPlayableToolBar->hide();
	}
}

/*!
	Shows/Hides the time signature properties tool bar according to the current state.
*/
void CAMainWin::updateTimeSigToolBar() {
	if (uiTimeSigType->isChecked() && mode()==InsertMode) {
		uiTimeSigBeats->setValue( musElementFactory()->timeSigBeats() );
		uiTimeSigBeat->setValue( musElementFactory()->timeSigBeat() );
		uiTimeSigToolBar->show();
	} else if (mode()==EditMode) {
		CAScoreViewPort *v = currentScoreViewPort();
		if (v && v->selection().size()) {
			CATimeSignature *timeSig = dynamic_cast<CATimeSignature*>(v->selection().at(0)->musElement());
			if (timeSig) {
				uiTimeSigBeats->setValue( timeSig->beats() );
				uiTimeSigBeat->setValue( timeSig->beat() );
				uiTimeSigToolBar->show();
			} else
				uiTimeSigToolBar->hide();
		}	
	} else
		uiTimeSigToolBar->hide();
}

/*!
	Shows/Hides the key signature properties tool bar according to the current state.
*/
void CAMainWin::updateKeySigToolBar() {
	if (uiInsertKeySig->isChecked() && mode()==InsertMode) {
		uiKeySig->setCurrentIndex((musElementFactory()->keySigNumberOfAccs()+7)*2 + ((musElementFactory()->keySigGender()==CAKeySignature::Minor)?1:0) );
		uiKeySigToolBar->show();
	} else if (mode()==EditMode) {
		CAScoreViewPort *v = currentScoreViewPort();
		if (v && v->selection().size()) {
			CAKeySignature *keySig = dynamic_cast<CAKeySignature*>(v->selection().at(0)->musElement());
			if (keySig) {
				uiKeySig->setCurrentIndex((keySig->numberOfAccidentals()+7)*2 + ((keySig->majorMinorGender()==CAKeySignature::Minor)?1:0) );
				uiKeySigToolBar->show();
			} else
				uiKeySigToolBar->hide();
		}	
	} else
		uiKeySigToolBar->hide();
}

/*!
	Shows/Hides the clef properties tool bar according to the current state.
*/
void CAMainWin::updateClefToolBar() {
	if ( uiClefType->isChecked() && mode()==InsertMode ) {
		uiClefOffset->setValue( musElementFactory()->clefOffset() );
		uiClefToolBar->show();
	} else if (mode()==EditMode) {
		CAScoreViewPort *v = currentScoreViewPort();
		if (v && v->selection().size()) {
			CAClef *clef = dynamic_cast<CAClef*>(v->selection().at(0)->musElement());
			if (clef) {
				uiClefOffset->setValue( CAClef::offsetToReadable(clef->offset()) );
				uiClefToolBar->show();
			} else
				uiClefToolBar->hide();
		}	
	} else
		uiClefToolBar->hide();
}

/*!
	Shows/Hides the function marking properties tool bar according to the current state.
*/
void CAMainWin::updateFMToolBar() {
	if (uiInsertFM->isChecked() && mode()==InsertMode) {
		uiFMFunction->setCurrentId( musElementFactory()->fmFunction()*(musElementFactory()->isFMFunctionMinor()?-1:1) );
		uiFMChordArea->setCurrentId( musElementFactory()->fmChordArea()*(musElementFactory()->isFMChordAreaMinor()?-1:1) );
		uiFMTonicDegree->setCurrentId( musElementFactory()->fmTonicDegree()*(musElementFactory()->isFMTonicDegreeMinor()?-1:1) );		
		uiFMEllipse->setChecked( musElementFactory()->isFMEllipse() );
		
		uiFMKeySig->setCurrentIndex((musElementFactory()->keySigNumberOfAccs()+7)*2 + ((musElementFactory()->keySigGender()==CAKeySignature::Minor)?1:0) );
		
		uiFMToolBar->show();
	} else if ( mode()==EditMode && currentScoreViewPort() &&
	            currentScoreViewPort()->selection().size() &&
	            dynamic_cast<CAFunctionMarking*>(currentScoreViewPort()->selection().at(0)->musElement()) ) {
		CAFunctionMarking *fm = dynamic_cast<CAFunctionMarking*>(currentScoreViewPort()->selection().at(0)->musElement());
		uiFMFunction->setCurrentId( fm->function()*(fm->isMinor()?-1:1) );
		uiFMChordArea->setCurrentId( fm->chordArea()*(fm->isChordAreaMinor()?-1:1) );
		uiFMTonicDegree->setCurrentId( fm->tonicDegree()*(fm->isTonicDegreeMinor()?-1:1) );
		uiFMEllipse->setChecked( fm->isPartOfEllipse() );
		
		signed char accs = CAKeySignature::keySigAccsFromString(fm->key());
		CAKeySignature::CAMajorMinorGender gender = CAKeySignature::keySigGenderFromString(fm->key());
		uiFMKeySig->setCurrentIndex((accs+7)*2 + ((gender==CAKeySignature::Minor)?1:0) );
		
		uiFMToolBar->show();
	} else
		uiFMToolBar->hide();
}

/*!
	Shows/Hides the dynamic marks properties tool bar according to the current state.
*/
void CAMainWin::updateDynamicToolBar() {
	if ( uiMarkType->isChecked() && uiMarkType->currentId()==CAMark::Dynamic && mode()==InsertMode) {
		uiDynamicText->setCurrentId( CADynamic::dynamicTextFromString(musElementFactory()->dynamicText()) );
		uiDynamicVolume->setValue( musElementFactory()->dynamicVolume() );
		uiDynamicCustomText->setText( musElementFactory()->dynamicText() );
		uiDynamicToolBar->show();
	} else if (mode()==EditMode) {
		CAScoreViewPort *v = currentScoreViewPort();
		if (v && v->selection().size()) {
			CADynamic *dynamic = dynamic_cast<CADynamic*>(v->selection().at(0)->musElement());
			if (dynamic) {
				uiDynamicText->setCurrentId( CADynamic::dynamicTextFromString(dynamic->text()) );
				uiDynamicVolume->setValue( dynamic->volume() );
				uiDynamicCustomText->setText( dynamic->text() );
				uiDynamicToolBar->show();
			} else
				uiDynamicToolBar->hide();
		}	
	} else
		uiDynamicToolBar->hide();
}

/*!
	Shows/Hides the fermata properties tool bar according to the current state.
*/
void CAMainWin::updateFermataToolBar() {
	if ( uiMarkType->isChecked() && uiMarkType->currentId()==CAMark::Fermata && mode()==InsertMode) {
		uiFermataType->setCurrentId( musElementFactory()->fermataType() );
		uiFermataToolBar->show();
	} else if (mode()==EditMode) {
		CAScoreViewPort *v = currentScoreViewPort();
		if (v && v->selection().size()) {
			CAFermata *f = dynamic_cast<CAFermata*>(v->selection().at(0)->musElement());
			if (f) {
				uiFermataType->setCurrentId( f->fermataType() );
				uiFermataToolBar->show();
			} else
				uiFermataToolBar->hide();
		}	
	} else
		uiFermataToolBar->hide();
}

/*!
	Shows/Hides the repeat mark properties tool bar according to the current state.
*/
void CAMainWin::updateRepeatMarkToolBar() {
	if ( uiMarkType->isChecked() && uiMarkType->currentId()==CAMark::RepeatMark && mode()==InsertMode) {
		if ( musElementFactory()->repeatMarkType()==CARepeatMark::Volta )
			uiRepeatMarkType->setCurrentId( musElementFactory()->repeatMarkVoltaNumber()*(-1)-1 );
		else
			uiRepeatMarkType->setCurrentId( musElementFactory()->repeatMarkType() );
		uiRepeatMarkToolBar->show();
	} else if (mode()==EditMode) {
		CAScoreViewPort *v = currentScoreViewPort();
		if (v && v->selection().size()) {
			CARepeatMark *r = dynamic_cast<CARepeatMark*>(v->selection().at(0)->musElement());
			if (r) {
				if ( r->repeatMarkType()==CARepeatMark::Volta )
					uiRepeatMarkType->setCurrentId( r->voltaNumber()*(-1)-1 );
				else
					uiRepeatMarkType->setCurrentId( r->repeatMarkType() );
				uiRepeatMarkToolBar->show();
			} else
				uiRepeatMarkToolBar->hide();
		}	
	} else
		uiRepeatMarkToolBar->hide();
}

/*!
	Shows/Hides the fingering properties tool bar according to the current state.
*/
void CAMainWin::updateFingeringToolBar() {
	if ( uiMarkType->isChecked() && uiMarkType->currentId()==CAMark::Fingering && mode()==InsertMode) {
		uiFinger->setCurrentId( musElementFactory()->fingeringFinger() );
		uiFingeringOriginal->setChecked( musElementFactory()->isFingeringOriginal() );
		uiFingeringToolBar->show();
	} else if (mode()==EditMode) {
		CAScoreViewPort *v = currentScoreViewPort();
		if (v && v->selection().size()) {
			CAFingering *f = dynamic_cast<CAFingering*>(v->selection().at(0)->musElement());
			if (f) {
				uiFinger->setCurrentId( f->finger() );
				uiFingeringOriginal->setChecked( f->isOriginal() );
				uiFingeringToolBar->show();
			} else
				uiFingeringToolBar->hide();
		}	
	} else
		uiFingeringToolBar->hide();
}

/*!
	Shows/Hides the tempo marks properties tool bar according to the current state.
*/
void CAMainWin::updateTempoToolBar() {
	if ( uiMarkType->isChecked() && uiMarkType->currentId()==CAMark::Tempo && mode()==InsertMode) {
		uiTempoBeat->setCurrentId( musElementFactory()->tempoBeat() * (musElementFactory()->tempoBeatDotted()?-1:1) );
		uiTempoBpm->setText( QString::number(musElementFactory()->tempoBpm()) );
		uiTempoToolBar->show();
	} else if (mode()==EditMode) {
		CAScoreViewPort *v = currentScoreViewPort();
		if (v && v->selection().size()) {
			CATempo *tempo = dynamic_cast<CATempo*>(v->selection().at(0)->musElement());
			if (tempo) {
				uiTempoBeat->setCurrentId( tempo->beat() * (tempo->beatDotted()?-1:1) );
				uiTempoBpm->setText( QString::number(tempo->bpm()) );
				uiTempoToolBar->show();
			} else
				uiTempoToolBar->hide();
		}	
	} else
		uiTempoToolBar->hide();
}

/*!
	Shows/Hides the instrument marks properties tool bar according to the current state.
*/
void CAMainWin::updateInstrumentToolBar() {
	if ( uiMarkType->isChecked() && uiMarkType->currentId()==CAMark::InstrumentChange && mode()==InsertMode) {
		uiInstrumentChange->setCurrentIndex( musElementFactory()->instrument() );
		uiInstrumentToolBar->show();
	} else if (mode()==EditMode) {
		CAScoreViewPort *v = currentScoreViewPort();
		if (v && v->selection().size()) {
			CAInstrumentChange *instrument = dynamic_cast<CAInstrumentChange*>(v->selection().at(0)->musElement());
			if (instrument) {
				uiInstrumentChange->setCurrentIndex( instrument->instrument() );
				uiInstrumentToolBar->show();
			} else
				uiInstrumentToolBar->hide();
		}	
	} else
		uiInstrumentToolBar->hide();

}

/*!
	Action on Edit->Copy.
*/
void CAMainWin::on_uiCopy_triggered() {
	if ( currentScoreViewPort() ) {
		copySelection( currentScoreViewPort() );
	}
}

/*!
	Action on Edit->Cut.
*/
void CAMainWin::on_uiCut_triggered() {
	if ( currentScoreViewPort() ) {
		stopPlayback();
		CACanorus::undo()->createUndoCommand( document(), tr("cut", "undo") );
		copySelection( currentScoreViewPort() );
		deleteSelection( currentScoreViewPort(), false, true, false ); // and don't make undo as we already make it
		CACanorus::undo()->pushUndoCommand();
	}	
}

/*!
	Action on Edit->Paste.
*/
void CAMainWin::on_uiPaste_triggered() {
	if ( currentScoreViewPort() ) {
		pasteAt( currentScoreViewPort()->lastMousePressCoords(), currentScoreViewPort() );
	}
}

/*!
	Backend for Edit->Copy.
*/
void CAMainWin::copySelection( CAScoreViewPort *v ) {
	if ( v->selection().size() ) {
		QList<CAMusElement*> list;
		
		for (int i=0; i<v->selection().size(); i++) {
			if (v->selection().at(i)->musElement() && !list.contains( v->selection().at(i)->musElement())) {
				list << v->selection().at(i)->musElement()->clone(); // Add clones to clipboard. They are destroyed in CAMimeData destructor!
			}
		}
		
		QApplication::clipboard()->setMimeData( new CAMimeData(list) );
	}
}

/*!
	Delete action.
	
	If \a deleteSyllables or \a deleteNotes is True, it completely removes the element.
	Otherwise it only replaces it with rest.
	
	If \a doUndo is True, it also creates undo. doUndo should be False when cutting.
*/
void CAMainWin::deleteSelection( CAScoreViewPort *v, bool deleteSyllables, bool deleteNotes, bool doUndo ) {
	if ( v->selection().size() ) {
		if (doUndo)
			CACanorus::undo()->createUndoCommand( document(), tr("deletion of elements", "undo") );
		
		QSet<CAMusElement*> musElemSet;
		for (int i=0; i<v->selection().size(); i++)
			musElemSet << v->selection().at(i)->musElement();
		
		// cleans up the set - removes empty elements and elements which get deleted automatically (eg. slurs, if both notes are deleted)
		for (QSet<CAMusElement*>::iterator i=musElemSet.begin(); i!=musElemSet.end();) {
			if (!(*i))
				i = musElemSet.erase(i);
			else if ((*i)->musElementType()==CAMusElement::Slur && musElemSet.contains(static_cast<CASlur*>(*i)->noteStart()))
				i = musElemSet.erase(i);
			else if ((*i)->musElementType()==CAMusElement::Slur && musElemSet.contains(static_cast<CASlur*>(*i)->noteEnd()))
				i = musElemSet.erase(i);
			else
				i++;
		}
		
		for (QSet<CAMusElement*>::const_iterator i=musElemSet.constBegin(); i!=musElemSet.constEnd(); i++) {
			if ( (*i)->musElementType()==CAMusElement::Note ||
			     (*i)->musElementType()==CAMusElement::Rest ) {
				CAPlayable *p = static_cast<CAPlayable*>(*i);
				if ( !dynamic_cast<CANote*>(p) || !static_cast<CANote*>(p)->isPartOfTheChord() ) {
					// find out the status of the rests in other voices
					QList<CAPlayable*> chord = p->staff()->getChord( p->timeStart() );
					int i;
					for (i=0; i<chord.size(); i++) {
						if ( chord[i]->voice()!=p->voice() &&
						    (chord[i]->musElementType()!=CAMusElement::Rest ||
						     chord[i]->timeStart()!=p->timeStart() ||
						     chord[i]->timeLength()!=p->timeLength()
						    )
						   )
							break;
					}
					
					if ( p->musElementType()==CAMusElement::Note && !deleteNotes || i!=chord.size() ) {
						// replace note with rest
						QList<CARest*> rests = CARest::composeRests( p->timeLength(), p->timeStart(), p->voice(), CARest::Normal );
						for (int i=0; i<rests.size(); i++)
							p->voice()->insert( p, rests[i] );
						
						for (int j=0; j<p->voice()->lyricsContextList().size(); j++) { // remove syllables
							CASyllable *removedSyllable =
								p->voice()->lyricsContextList().at(j)->syllableAtTimeStart(p->timeStart());
							
							musElemSet.remove(removedSyllable);
						}
					} else {
						// actually remove the note or rest and shift other elements back if only rests in other voices present
						for (i=0; i<chord.size(); i++) {              // remove any rests from other voices
							if ( chord[i]->voice()!=p->voice() ) {
								musElemSet.remove( chord[i] );
								delete chord[i];
							}
						}
						
						for (int j=0; j<p->voice()->lyricsContextList().size(); j++) // remove and shift syllables
							p->voice()->lyricsContextList().at(j)->removeSyllableAtTimeStart(p->timeStart());
					}
				}
				
				p->voice()->remove( p, true );
				for (int j=0; j<p->voice()->lyricsContextList().size(); j++) {
					p->voice()->lyricsContextList().at(j)->repositSyllables();					
				}
				delete p;
			} else if ( (*i)->musElementType()==CAMusElement::Syllable ) {
				if ( deleteSyllables ) {
					CALyricsContext *lc = static_cast<CALyricsContext*>((*i)->context()); 
					(*i)->context()->remove(*i);  // actually removes the syllable if SHIFT is pressed
					lc->repositSyllables();
				} else {
					static_cast<CASyllable*>(*i)->clear(); // only clears syllable's text
				}
			} else if ( (*i)->musElementType()==CAMusElement::FunctionMarking ) {
				if ( deleteSyllables ) {
					CAFunctionMarkingContext *fmc = static_cast<CAFunctionMarkingContext*>((*i)->context()); 
					(*i)->context()->remove(*i);  // actually removes the function if SHIFT is pressed
					fmc->repositFunctions();
				} else {
					static_cast<CAFunctionMarking*>(*i)->clear(); // only clears the function
				}
			} else if ( (*i)->musElementType()==CAMusElement::Mark ) {
				delete *i; // also removes itself from associated elements
			} else {
				(*i)->context()->remove(*i);
			}
		}
		if (doUndo)
			CACanorus::undo()->pushUndoCommand();
		
		v->clearSelection();
		CACanorus::rebuildUI(document(), v->sheet());
	}	
}

/*!
	Backend for Edit->Paste.
*/
void CAMainWin::pasteAt( const QPoint coords, CAScoreViewPort *v ) {
	if ( QApplication::clipboard()->mimeData() &&
	     dynamic_cast<const CAMimeData*>(QApplication::clipboard()->mimeData()) &&
	     v->currentContext() &&
	     v->currentContext()->drawableContextType()==CADrawableContext::DrawableStaff ) {
		CACanorus::undo()->createUndoCommand( document(), tr("paste", "undo") );
		
		CAStaff *staff = static_cast<CAStaff*>( v->currentContext()->context() ); 
		CAVoice *voice = staff->voiceAt( uiVoiceNum->getRealValue() ? uiVoiceNum->getRealValue()-1 : uiVoiceNum->getRealValue() );
		CADrawableMusElement *drawableRight = v->nearestRightElement(coords.x(), coords.y(), voice);
		CAMusElement *right = 0;
		if (drawableRight)
			right = drawableRight->musElement();
		
		QList<CAMusElement*> newEltList;
		QList<CAMusElement*> eltList = static_cast<const CAMimeData*>(QApplication::clipboard()->mimeData())->musElements();
		for ( int i=0; i<eltList.size(); i++ ) {
			CAMusElement *newElt;
			if ( eltList[i]->isPlayable() ) {
				newElt = static_cast<CAPlayable*>(eltList[i])->clone(voice);
				if ( eltList[i]->musElementType()==CAMusElement::Note &&
				     i>0 && eltList[i-1]->musElementType()==CAMusElement::Note &&
				     static_cast<CANote*>(eltList[i-1])->voice()==static_cast<CANote*>(eltList[i])->voice() &&
				     eltList[i-1]->timeStart()==eltList[i]->timeStart() ) {
					voice->insert( newEltList.last(), newElt, true );
				} else {
					voice->insert( right, newElt, false );
				}
			} else {
				newElt = eltList[i]->clone();
				newElt->setContext( staff );
				newElt->setTimeStart( right?right->timeEnd():0);
				voice->insert( right, newElt );
			}
			newEltList << newElt;
		}
		
		staff->synchronizeVoices();
		
		CACanorus::undo()->pushUndoCommand();
		CACanorus::rebuildUI( document(), currentSheet() );
		
		// select paste elements
		currentScoreViewPort()->clearSelection();
		for (int i=0; i<newEltList.size(); i++)
			currentScoreViewPort()->addToSelection( newEltList[i] );
		currentScoreViewPort()->repaint();
	}
}

void CAMainWin::on_uiDynamicText_toggled(bool checked, int t) {
	if (t==CADynamic::Custom)
		return;
	
	QString text = CADynamic::dynamicTextToString(static_cast<CADynamic::CADynamicText>(t));
	if (mode()==InsertMode) {
		musElementFactory()->setDynamicText( text );
		uiDynamicCustomText->setText(text);
	} else if ( mode()==EditMode ) {
		CAScoreViewPort *v = currentScoreViewPort();
		if ( v && v->selection().size() ) {
			CADynamic *dynamic = dynamic_cast<CADynamic*>(v->selection().at(0)->musElement());
			if ( dynamic ) {
				dynamic->setText( text );
				CACanorus::rebuildUI(document(), currentSheet());
			}
		}
	}
}

void CAMainWin::on_uiDynamicVolume_valueChanged(int vol) {
	if (mode()==InsertMode) {
		musElementFactory()->setDynamicVolume( vol );
	} else if ( mode()==EditMode ) {
		CAScoreViewPort *v = currentScoreViewPort();
		if ( v && v->selection().size() ) {
			CADynamic *dynamic = dynamic_cast<CADynamic*>(v->selection().at(0)->musElement());
			if ( dynamic ) {
				dynamic->setVolume( vol );
				CACanorus::rebuildUI(document(), currentSheet());
			}
		}
	}
}

void CAMainWin::on_uiDynamicCustomText_returnPressed() {
	QString text = uiDynamicCustomText->text();
	CADynamic::CADynamicText t = CADynamic::dynamicTextFromString(text);
	
	uiDynamicText->setCurrentId(t);
	if (mode()==InsertMode) {
		musElementFactory()->setDynamicText( text );
	} else if ( mode()==EditMode ) {
		CAScoreViewPort *v = currentScoreViewPort();
		if ( v && v->selection().size() ) {
			CADynamic *dynamic = dynamic_cast<CADynamic*>(v->selection().at(0)->musElement());
			if ( dynamic ) {
				dynamic->setText( text );
				CACanorus::rebuildUI(document(), currentSheet());
			}
		}
	}
}

void CAMainWin::on_uiInstrumentChange_activated( int index ) {
	if (mode()==InsertMode) {
		musElementFactory()->setInstrument( index );
	} else if ( mode()==EditMode ) {
		CAScoreViewPort *v = currentScoreViewPort();
		CACanorus::undo()->createUndoCommand( document(), tr("change fermata type", "undo") );
		
		for ( int i=0; i<v->selection().size(); i++ ) {
			CAInstrumentChange *instrument = dynamic_cast<CAInstrumentChange*>( v->selection().at(i)->musElement() );
			
			if ( instrument) {
				instrument->setInstrument( index);
			}
		}
		
		CACanorus::undo()->pushUndoCommand();
		CACanorus::rebuildUI( document(), currentSheet() );
	}
}

void CAMainWin::on_uiFermataType_toggled( bool checked, int t ) {
	CAFermata::CAFermataType type = static_cast<CAFermata::CAFermataType>( t );
	
	if ( mode()==InsertMode ) {
		musElementFactory()->setFermataType( type );
	} else
	if ( mode()==EditMode && currentScoreViewPort() && currentScoreViewPort()->selection().size()) {
		CAScoreViewPort *v = currentScoreViewPort();
		CACanorus::undo()->createUndoCommand( document(), tr("change fermata type", "undo") );
		
		for ( int i=0; i<v->selection().size(); i++ ) {
			CAFermata *fm = dynamic_cast<CAFermata*>( v->selection().at(i)->musElement() );
			
			if ( fm ) {
				fm->setFermataType( type );
			}
		}
		
		CACanorus::undo()->pushUndoCommand();
		CACanorus::rebuildUI( document(), currentSheet() );
	}
}

void CAMainWin::on_uiFinger_toggled( bool checked, int t ) {
	CAFingering::CAFingerNumber type = static_cast<CAFingering::CAFingerNumber>( t );
	
	if ( mode()==InsertMode ) {
		musElementFactory()->setFingeringFinger( type );
	} else
	if ( mode()==EditMode && currentScoreViewPort() && currentScoreViewPort()->selection().size()) {
		CAScoreViewPort *v = currentScoreViewPort();
		CACanorus::undo()->createUndoCommand( document(), tr("change finger", "undo") );
		
		for ( int i=0; i<v->selection().size(); i++ ) {
			CAFingering *f = dynamic_cast<CAFingering*>( v->selection().at(i)->musElement() );
			
			if ( f ) {
				f->setFinger( type );
			}
		}
		
		CACanorus::undo()->pushUndoCommand();
		CACanorus::rebuildUI( document(), currentSheet() );
	}
}

void CAMainWin::on_uiFingeringOriginal_toggled( bool checked ) {
	if ( mode()==InsertMode ) {
		musElementFactory()->setFingeringOriginal( checked );
	} else
	if ( mode()==EditMode && currentScoreViewPort() && currentScoreViewPort()->selection().size()) {
		CAScoreViewPort *v = currentScoreViewPort();
		CACanorus::undo()->createUndoCommand( document(), tr("change finger original property", "undo") );
		
		for ( int i=0; i<v->selection().size(); i++ ) {
			CAFingering *f = dynamic_cast<CAFingering*>( v->selection().at(i)->musElement() );
			
			if ( f ) {
				f->setOriginal( checked );
			}
		}
		
		CACanorus::undo()->pushUndoCommand();
		CACanorus::rebuildUI( document(), currentSheet() );
	}	
}

void CAMainWin::on_uiRepeatMarkType_toggled( bool checked, int t ) {
	CARepeatMark::CARepeatMarkType type;
	int voltaNumber;
	if (t >= 0) {
		type = static_cast<CARepeatMark::CARepeatMarkType>( t );
		voltaNumber = 0;
	} else {
		type = CARepeatMark::Volta;
		voltaNumber = t*(-1)-1;
	}
	
	if ( mode()==InsertMode ) {
		musElementFactory()->setRepeatMarkType( type );
		musElementFactory()->setRepeatMarkVoltaNumber( voltaNumber );
	} else
	if ( mode()==EditMode && currentScoreViewPort() && currentScoreViewPort()->selection().size()) {
		CAScoreViewPort *v = currentScoreViewPort();
		CACanorus::undo()->createUndoCommand( document(), tr("change repeat mark", "undo") );
		
		for ( int i=0; i<v->selection().size(); i++ ) {
			CARepeatMark *r = dynamic_cast<CARepeatMark*>( v->selection().at(i)->musElement() );
			
			if ( r ) {
				r->setRepeatMarkType( type );
				r->setVoltaNumber(voltaNumber);
			}
		}
		
		CACanorus::undo()->pushUndoCommand();
		CACanorus::rebuildUI( document(), currentSheet() );
	}
}

void CAMainWin::on_uiTempoBeat_toggled( bool checked, int t ) {
	CAPlayable::CAPlayableLength length = static_cast<CAPlayable::CAPlayableLength>( t<0?t*(-1):t );
	
	if ( mode()==InsertMode ) {
		musElementFactory()->setTempoBeat( length );
		musElementFactory()->setTempoBeatDotted( t<0?1:0 );
	} else
	if ( mode()==EditMode && currentScoreViewPort() && currentScoreViewPort()->selection().size()) {
		CAScoreViewPort *v = currentScoreViewPort();
		CACanorus::undo()->createUndoCommand( document(), tr("change tempo beat", "undo") );
		
		for ( int i=0; i<v->selection().size(); i++ ) {
			CATempo *tempo = dynamic_cast<CATempo*>( v->selection().at(i)->musElement() );
			
			if ( tempo ) {
				tempo->setBeat( length );
				tempo->setBeatDotted( t<0?1:0 );
			}
		}
		
		CACanorus::undo()->pushUndoCommand();
		CACanorus::rebuildUI( document(), currentSheet() );
	}	
}

void CAMainWin::on_uiTempoBpm_returnPressed() {
	QString text = uiTempoBpm->text();
	int bpm = text.toInt();
	
	if (mode()==InsertMode) {
		musElementFactory()->setTempoBpm( bpm );
	} else if ( mode()==EditMode ) {
		CAScoreViewPort *v = currentScoreViewPort();
		CACanorus::undo()->createUndoCommand( document(), tr("change tempo bpm", "undo") );
		
		for ( int i=0; i<v->selection().size(); i++ ) {
			CATempo *tempo = dynamic_cast<CATempo*>( v->selection().at(i)->musElement() );
			
			if ( tempo ) {
				tempo->setBpm( bpm );
			}
		}
		
		CACanorus::undo()->pushUndoCommand();
		CACanorus::rebuildUI( document(), currentSheet() );
	}
}

void CAMainWin::on_uiOpenRecent_aboutToShow() {
	while ( uiOpenRecent->actions().size() )
		delete uiOpenRecent->actions().at(0);
	
	for (int i=0; i<CACanorus::recentDocumentList().size(); i++) {
		QAction *a = new QAction( CACanorus::recentDocumentList()[i], this );
		uiOpenRecent->addAction( a );
		connect( a, SIGNAL(triggered()), this, SLOT(onUiOpenRecentDocumentTriggered()) );
	}
}

void CAMainWin::onUiOpenRecentDocumentTriggered() {
	bool success =
		openDocument( CACanorus::recentDocumentList().at(
				uiOpenRecent->actions().indexOf( static_cast<QAction*>(sender()) )
		) );
	
	if (!success) {
		CACanorus::removeRecentDocument( CACanorus::recentDocumentList().at(
				uiOpenRecent->actions().indexOf( static_cast<QAction*>(sender()) )
		) );
	}
}

/*!
	\var CADocument *CAMainWin::_document
	Pointer to the main window's document it represents.
	Null if no document opened.
	
	\sa document()
*/

/*!
	\var CAMode CAMainWin::_mode
	Main window's current mode (Select mode, Edit mode, Playback mode, Insert mode etc.).
	
	\sa CAMode, mode(), setMode()
*/

/*!
	\var QList<CAViewPort*> CAMainWin::_viewPortList
	List of all available viewports for any sheet in this main window.
*/

/*!
	\var CAViewPort* CACanorus::_currentViewPort
	Currently active viewport. Only one viewport per main window can be active.
*/

/*!
	\var bool CACanorus::_animatedScroll
	Animates mouse scroll/zoom. This gives a better user experience but slows down things.
*/

/*!
	\var bool CACanorus::_lockScrollPlayback
	If True, locks the scroll UI while playback.
	If False, user can scroll freely.
*/

/*!
	\var CAViewPort* CAMainWin::_playbackViewPort
	Viewport needed to be updated when playback is active.
*/

/*!
	\var QTimer* CACanorus::_repaintTimer
	Used when playback is active to repaint the playback viewport.
	\todo Viewport should be repainted only when needed, not constantly as now. This should result in much less resource hunger.
*/

/*!
	\var CAMusElementFactory *CACanorus::_musElementFactory
	Factory for creating/configuring music elements before actually placing them.
*/

/*!
	\var bool CAMainWin::_rebuildUILock
	Lock to avoid recursive rebuilds of the GUI viewports.
*/
