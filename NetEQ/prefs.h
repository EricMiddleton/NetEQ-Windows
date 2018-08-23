// prefs.h

#pragma once

class CPrefs {
public:
    IMMDevice *m_pMMDevice;
    HMMIO m_hFile;
    bool m_bInt16;
    PWAVEFORMATEX m_pwfx;
    LPCWSTR m_szFilename;

    // set hr to S_FALSE to abort but return success
    CPrefs(int argc, LPCWSTR argv[], HRESULT &hr);
    ~CPrefs();

};

HRESULT get_specific_device(LPCWSTR szLongName, IMMDevice **ppMMDevice);