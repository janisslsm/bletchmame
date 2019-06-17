/***************************************************************************

    dlgimages.h

    Images (File Manager) dialog

***************************************************************************/

#pragma once

#ifndef DLGIMAGES_H
#define DLGIMAGES_H

#include <vector>
#include <memory>

// pesky Win32 declaration
#ifdef LoadImage
#undef LoadImage
#endif

struct Image;
class IImagesHost
{
public:
	virtual const std::vector<Image> GetImages() = 0;
	virtual void SetOnImagesChanged(std::function<void()> &&func) = 0;
	virtual const wxString &GetWorkingDirectory() const = 0;
	virtual void SetWorkingDirectory(wxString &&dir) = 0;
	virtual const std::vector<wxString> &GetExtensions(const wxString &tag) const = 0;
	virtual void LoadImage(const wxString &tag, wxString &&path) = 0;
	virtual void UnloadImage(const wxString &tag) = 0;
};


void show_images_dialog(IImagesHost &host);
bool show_images_dialog_cancellable(IImagesHost &host);

#endif // DLGIMAGES_H