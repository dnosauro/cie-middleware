#include "stdafx.h"
#include "IAS.h"
#include "ModuleInfo.h"
#include "Message.h"
#include "Pin.h"
#include "Verifica.h"
#include "SHA256.h"
#include <functional>
#include "ASNParser.h"
#include "safeDesktop.h"
#include <atlbase.h>

extern CModuleInfo moduleInfo;
extern "C" DWORD WINAPI CardAcquireContext(IN PCARD_DATA pCardData, __in DWORD dwFlags);

#ifdef _WIN64
	#pragma comment(linker, "/export:AbilitaCIE")
#else
	#pragma comment(linker, "/export:AbilitaCIE=_AbilitaCIE@16")
#endif

struct threadData {
	HWND *progWin;
	HDESK hDesk;
};

DWORD WINAPI _abilitaCIE(
	LPVOID lpThreadParameter) {
	init_main_func
		char *PAN = (char *)lpThreadParameter;
	BOOL ris;

	String container;
	container.printf("CIE-%s", PAN);

	try {

		CSHA256 sha256;
		std::map<BYTE, ByteDynArray> hashSet;
		BYTE* data;
		DWORD len = 0;
		ByteDynArray CertCIE;
		ByteDynArray SOD;
		ByteDynArray IdServizi;
		safeDesktop *desk = nullptr;
		Allocator<safeDesktop> alloDesktop(desk);

		SCARDCONTEXT hSC;

		SCardEstablishContext(SCARD_SCOPE_SYSTEM, nullptr, nullptr, &hSC);
		char *readers = nullptr;
		len = SCARD_AUTOALLOCATE;
		SCardListReaders(hSC, nullptr, (char*)&readers, &len);
		char *curreader = readers;
		bool foundCIE = false;
		for (; curreader[0] != 0; curreader += strlen(curreader) + 1) {
			CARD_DATA cData;
			ZeroMemory(&cData, sizeof(cData));
			cData.dwVersion = 7;
			cData.hSCardCtx = hSC;
			{
				safeConnection conn(hSC, curreader, SCARD_SHARE_SHARED);
				if (conn.hCard == NULL)
					continue;

				{
					safeTransaction checkTran(conn, SCARD_LEAVE_CARD);
					if (!checkTran.isLocked())
						continue;

					len = SCARD_AUTOALLOCATE;
					cData.hScard = conn;
					SCardGetAttrib(cData.hScard, SCARD_ATTR_ATR_STRING, (BYTE*)&cData.pbAtr, &len);
					cData.pfnCspAlloc = (PFN_CSP_ALLOC)CryptMemAlloc;
					cData.pfnCspReAlloc = (PFN_CSP_REALLOC)CryptMemRealloc;
					cData.pfnCspFree = (PFN_CSP_FREE)CryptMemFree;
					cData.cbAtr = len;
					cData.pwszCardName = L"CIE";
					if (CardAcquireContext(&cData, 0) != 0)
						continue;
				}
				foundCIE = true;
				if (desk == nullptr)
					desk = new safeDesktop("AbilitaCIE");

				CMessage aa(IDB_BACKGROUND, MB_OKCANCEL,
					"Premere OK per effettuare la verifica di autenticit�",
					"e abilitare l'uso della CIE su questo PC");

				if (aa.DoModal() == IDOK) {

					CPin pin(IDB_BACKGROUND, "Inserire il PIN della CIE");
					if (pin.DoModal() == IDOK) {
						try {

							safeTransaction Tran(conn, SCARD_LEAVE_CARD);
							if (!Tran.isLocked())
								continue;

							len = 0;
							CardReadFile(&cData, DirCIE, EfIdServizi, 0, &data, &len);
							IdServizi = ByteArray(data, len);
							cData.pfnCspFree(data);
							sha256.Digest(IdServizi.left(12), hashSet[0xa1]);

							len = 0;
							CardReadFile(&cData, DirCIE, EfSOD, 0, &data, &len);
							SOD = ByteArray(data, len);
							cData.pfnCspFree(data);

							ByteDynArray IntAuth; len = 0;
							CardReadFile(&cData, DirCIE, EfIntAuth, 0, &data, &len);
							IntAuth = ByteArray(data, len);
							cData.pfnCspFree(data);
							sha256.Digest(IntAuth.left(GetASN1DataLenght(IntAuth)), hashSet[0xa4]);

							ByteDynArray IntAuthServizi; len = 0;
							CardReadFile(&cData, DirCIE, EfIntAuthServizi, 0, &data, &len);
							IntAuthServizi = ByteArray(data, len);
							cData.pfnCspFree(data);
							sha256.Digest(IntAuthServizi.left(GetASN1DataLenght(IntAuthServizi)), hashSet[0xa5]);

							ByteDynArray DH; len = 0;
							CardReadFile(&cData, DirCIE, EfDH, 0, &data, &len);
							DH = ByteArray(data, len);
							cData.pfnCspFree(data);

							sha256.Digest(DH.left(GetASN1DataLenght(DH)), hashSet[0x1b]);
							auto ias = ((IAS*)cData.pvVendorSpecific);
							if (IdServizi != ByteArray((BYTE*)PAN, strlen(PAN)))
								continue;

							DWORD id;
							HWND progWin = nullptr;
							struct threadData th;
							th.progWin = &progWin;
							th.hDesk = (HDESK)(*desk);
							CreateThread(nullptr, 0, [](LPVOID lpThreadParameter) -> DWORD {
								struct threadData *th = (struct threadData*)lpThreadParameter;
								SetThreadDesktop(th->hDesk);
								CVerifica ver(IDB_BACKGROUND, th->progWin);
								ver.DoModal();
								return 0;
							}, &th, 0, &id);

							ias->Callback = [](int prog, char *desc, void *data) {
								HWND progWin = *(HWND*)data;
								if (progWin != nullptr)
									SendMessage(progWin, WM_COMMAND, 100 + prog, (LPARAM)desc);
							};
							ias->CallbackData = &progWin;

							DWORD attempts = -1;

							ris = CardAuthenticateEx(&cData, ROLE_USER, 0, (BYTE*)pin.PIN, strlen(pin.PIN), nullptr, 0, &attempts);
							if (ris == SCARD_W_WRONG_CHV) {
								if (progWin != nullptr)
									SendMessage(progWin, WM_COMMAND, 100 + 7, (LPARAM)"");
								String num;
								if (attempts > 0)
									num.printf("Sono rimasti %i tentativi prima del blocco", attempts);
								else
									num = "";
								CMessage aa(IDB_BACKGROUND, MB_OK,
									"PIN Errato",
									num.lock());
								aa.DoModal();
								break;
							}
							else if (ris == SCARD_W_CHV_BLOCKED) {
								if (progWin != nullptr)
									SendMessage(progWin, WM_COMMAND, 100 + 7, (LPARAM)"");
								CMessage aa(IDB_BACKGROUND, MB_OK,
									"",
									"Il PIN � bloccato. Pu� esere sbloccato verificando il PUK");
								aa.DoModal();
								break;
							}
							else if (ris != 0)
								throw CStringException("Autenticazione fallita");

							if (progWin != nullptr)
								SendMessage(progWin, WM_COMMAND, 100 + 4, (LPARAM)"Lettura certificato");

							ByteDynArray Serial; len = 0;
							CardReadFile(&cData, DirCIE, EfSerial, 0, &data, &len);
							Serial = ByteArray(data, len);
							cData.pfnCspFree(data);
							sha256.Digest(Serial.left(9), hashSet[0xa2]);
							len = 0;
							CardReadFile(&cData, DirCIE, EfCertCIE, 0, &data, &len);
							CertCIE = ByteArray(data, len);
							cData.pfnCspFree(data);
							sha256.Digest(CertCIE.left(GetASN1DataLenght(CertCIE)), hashSet[0xa3]);

							if (progWin != nullptr)
								SendMessage(progWin, WM_COMMAND, 100 + 5, (LPARAM)"Verifica SOD");
							ias->VerificaSOD(SOD, hashSet);

							if (progWin != nullptr)
								SendMessage(progWin, WM_COMMAND, 100 + 6, (LPARAM)"Cifratura dati");
							ias->SetCertificate(PAN, CertCIE);
							if (progWin != nullptr)
								SendMessage(progWin, WM_COMMAND, 100 + 7, (LPARAM)"");

							Tran.unlock();

							CMessage aa(IDB_BACKGROUND, MB_OK, "",
								"La CIE � abilitata all'uso");
							aa.DoModal();
						}
						catch (CBaseException &ex) {
							String dump;
							ex.DumpTree(dump);
							CMessage aa(IDB_BACKGROUND, MB_OK,
								"Si � verificato un errore nella verifica di",
								"autenticit� del documento");

							aa.DoModal();
							break;
						}
					}
					break;
				}
			}
		}
		if (!foundCIE) {
			if (desk == nullptr)
				desk = new safeDesktop("AbilitaCIE");
			String num;
			num.printf("%s nei lettori di smart card", PAN);
			CMessage aa(IDB_BACKGROUND, MB_OK,
				"Impossibile trovare la CIE con Numero Identificativo",
				num.lock());
			aa.DoModal();
		}
	}
	catch (CBaseException &ex) {
		String dump;
		ex.DumpTree(dump);
		MessageBox(nullptr, String().printf("Si � verificato un errore nella verifica di autenticit� del documento :%s", dump.lock()).lock(), "CIE", MB_OK);
	}

	return 0;
	exit_main_func
	return E_UNEXPECTED;
}


extern "C" int CALLBACK AbilitaCIE(
	_In_ HINSTANCE hInstance,
	_In_ HINSTANCE hPrevInstance,
	_In_ LPSTR     lpCmdLine,
	_In_ int       nCmdShow
	)
{
	if (_AtlWinModule.cbSize != sizeof(_ATL_WIN_MODULE)) {
		_AtlWinModule.cbSize = sizeof(_ATL_WIN_MODULE);
		AtlWinModuleInit(&_AtlWinModule);
	}
	WNDCLASS wndClass;
	GetClassInfo(NULL, WC_DIALOG, &wndClass);
	wndClass.hInstance = (HINSTANCE)moduleInfo.getModule();
	wndClass.style |= CS_DROPSHADOW;
	wndClass.lpszClassName = "CIEDialog";
	RegisterClass(&wndClass);

	ODS("Start AbilitaCIE");
	if (!CheckOneInstance("CIEAbilitaOnce")) {
		ODS("Already running AbilitaCIE");
		return 0;
	}
	DWORD id;
	HANDLE thread = CreateThread(nullptr, 0, _abilitaCIE, lpCmdLine, 0, &id);
	WaitForSingleObject(thread, INFINITE);
	ODS("End AbilitaCIE");
	return 0;
}
