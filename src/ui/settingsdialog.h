/*!
	Copyright (c) 2006-2007, Matevž Jekovec, Canorus development team
	All Rights Reserved. See AUTHORS for a complete list of authors.
	
	Licensed under the GNU GENERAL PUBLIC LICENSE. See COPYING for details.
*/

#ifndef MIDISETUPDIALOG_H_
#define MIDISETUPDIALOG_H_

#include <QWidget>
#include <QPoint>
#include <QColorDialog>

#include "ui_settingsdialog.h"

class CASheet;

class CASettingsDialog : public QDialog, private Ui::uiSettingsDialog {
	Q_OBJECT
	
public:
	enum CASettingsPage {
		UndefinedSettings = -1,
		EditorSettings = 0,
		AppearanceSettings  = 1,
		LoadSaveSettings = 2,
		PlaybackSettings = 3
	};
	
	CASettingsDialog( CASettingsPage currentPage, QWidget *parent=0 );
	~CASettingsDialog();
	
private slots:
	void on_uiButtonBox_clicked( QAbstractButton* );
	void on_uiSettingsList_currentItemChanged( QListWidgetItem*, QListWidgetItem* );
	
	void on_uiDocumentsDirectoryBrowse_clicked(bool);
	void on_uiDocumentsDirectoryRevert_clicked(bool);
	
	void on_uiBackgroundColor_clicked(bool);
	void on_uiBackgroundRevert_clicked(bool);
	void on_uiForegroundColor_clicked(bool);
	void on_uiForegroundRevert_clicked(bool);
	void on_uiSelectionColor_clicked(bool);
	void on_uiSelectionRevert_clicked(bool);
	void on_uiSelectionAreaColor_clicked(bool);
	void on_uiSelectionAreaRevert_clicked(bool);
	void on_uiSelectedContextColor_clicked(bool);
	void on_uiSelectedContextRevert_clicked(bool);
	void on_uiHiddenElementsColor_clicked(bool);
	void on_uiHiddenElementsRevert_clicked(bool);
	void on_uiDisabledElementsColor_clicked(bool);
	void on_uiDisabledElementsRevert_clicked(bool);
	
private:
	void setupPages( CASettingsPage currentPage=EditorSettings );
	void buildPreviewSheet();
	void applySettings();
	
	// Pages temporary variables
	CASheet *_previewSheet;
	QMap<int, QString> _midiInPorts;
	QMap<int, QString> _midiOutPorts;
};
#endif /* MIDISETUPDIALOG_H_ */