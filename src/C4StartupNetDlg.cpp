/*
 * LegacyClonk
 *
 * Copyright (c) RedWolf Design
 * Copyright (c) 2006, Sven2
 * Copyright (c) 2013, The OpenClonk Team and contributors
 * Copyright (c) 2017-2019, The LegacyClonk Team and contributors
 *
 * Distributed under the terms of the ISC license; see accompanying file
 * "COPYING" for details.
 *
 * "Clonk" is a registered trademark of Matthes Bender, used with permission.
 * See accompanying file "TRADEMARK" for details.
 *
 * To redistribute this file separately, substitute the full license texts
 * for the above references.
 */

// Startup screen for non-parameterized engine start: Network game selection dialog

#include <C4Include.h>
#include <C4StartupNetDlg.h>

#include <C4StartupScenSelDlg.h>
#include <C4StartupMainDlg.h>
#include <C4Game.h>
#include <C4Log.h>
#include "C4ChatDlg.h"

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#include <cassert>

// C4StartupNetListEntry

C4StartupNetListEntry::C4StartupNetListEntry(C4GUI::ListBox *pForListBox, C4GUI::Element *pInsertBefore, C4StartupNetDlg *pNetDlg)
	: pList(pForListBox), pRefClient(nullptr), pRef(nullptr), iTimeout(0), eQueryType(NRQT_Unknown), fError(false), fIsCollapsed(false), pNetDlg(pNetDlg), fIsSmall(false), fIsEnabled(true), iInfoIconCount(0), fIsImportant(false), iSortOrder(0), iNumFails(0), iInfoLink(-1)
{
	// calc height
	int32_t iLineHgt = C4GUI::GetRes()->TextFont.GetLineHeight(), iHeight = iLineHgt * 2 + 4;
	// add icons - normal icons use small size, only animated netgetref uses full size
	rctIconLarge.Set(0, 0, iHeight, iHeight);
	int32_t iSmallIcon = iHeight * 2 / 3; rctIconSmall.Set((iHeight - iSmallIcon) / 2, (iHeight - iSmallIcon) / 2, iSmallIcon, iSmallIcon);
	pIcon = new C4GUI::Icon(rctIconSmall, C4GUI::Ico_Host);
	AddElement(pIcon);
	SetBounds(pIcon->GetBounds());
	// add to listbox (will get resized horizontally and moved)
	pForListBox->InsertElement(this, pInsertBefore);
	// add status icons and text labels now that width is known
	CStdFont *pUseFont = &(C4GUI::GetRes()->TextFont);
	int32_t iIconSize = pUseFont->GetLineHeight();
	C4Rect rcIconRect = GetContainedClientRect();
	int32_t iThisWdt = rcIconRect.Wdt;
	rcIconRect.x = iThisWdt - iIconSize * (iInfoIconCount + 1);
	rcIconRect.Wdt = rcIconRect.Hgt = iIconSize;
	for (int32_t iIcon = 0; iIcon < MaxInfoIconCount; ++iIcon)
	{
		AddElement(pInfoIcons[iIcon] = new C4GUI::Icon(rcIconRect, C4GUI::Ico_None));
		rcIconRect.x -= rcIconRect.Wdt;
	}
	C4Rect rcLabelBounds;
	rcLabelBounds.x = iHeight + 3;
	rcLabelBounds.Hgt = iLineHgt;
	for (int i = 0; i < InfoLabelCount; ++i)
	{
		C4GUI::Label *pLbl;
		rcLabelBounds.y = 1 + i * (iLineHgt + 2);
		rcLabelBounds.Wdt = iThisWdt - rcLabelBounds.x - 1;
		if (!i) rcLabelBounds.Wdt -= iLineHgt; // leave space for topright extra icon
		AddElement(pLbl = pInfoLbl[i] = new C4GUI::Label("", rcLabelBounds, ALeft, C4GUI_CaptionFontClr));
		// label will have collapsed due to no text: Repair it
		pLbl->SetAutosize(false);
		pLbl->SetBounds(rcLabelBounds);
	}
	// update small state, which will resize this to a small entry
	UpdateSmallState();
	// Set*-function will fill icon and text and calculate actual size
}

C4StartupNetListEntry::~C4StartupNetListEntry()
{
	ClearRef();
}

void C4StartupNetListEntry::DrawElement(C4FacetEx &cgo)
{
	typedef C4GUI::Window ParentClass;
	// background if important and not selected
	if (fIsImportant && !IsSelectedChild(this))
	{
		int32_t x1 = cgo.X + cgo.TargetX + rcBounds.x;
		int32_t y1 = cgo.Y + cgo.TargetY + rcBounds.y;
		lpDDraw->DrawBoxDw(cgo.Surface, x1, y1, x1 + rcBounds.Wdt, y1 + rcBounds.Hgt, C4GUI_ImportantBGColor);
	}
	// inherited
	ParentClass::DrawElement(cgo);
}

void C4StartupNetListEntry::ClearRef()
{
	// del old ref data
	if (pRefClient)
	{
		C4InteractiveThread &Thread = Application.InteractiveThread;
		Thread.RemoveProc(pRefClient);
		delete pRefClient; pRefClient = nullptr;
	}
	delete pRef; pRef = nullptr;
	eQueryType = NRQT_Unknown;
	iTimeout = iRequestTimeout = 0;
	fError = false;
	sError.Clear();
	int32_t i;
	for (i = 0; i < InfoLabelCount; ++i) sInfoText[i].Clear();
	iInfoLink = -1;
	InvalidateStatusIcons();
	sRefClientAddress.Clear();
	fIsEnabled = true;
	fIsImportant = false;
}

const char *C4StartupNetListEntry::GetQueryTypeName(QueryType eQueryType)
{
	switch (eQueryType)
	{
	case NRQT_GameDiscovery: return LoadResStr("IDS_NET_QUERY_LOCALNET");
	case NRQT_Masterserver:  return LoadResStr("IDS_NET_QUERY_MASTERSRV");
	case NRQT_DirectJoin:    return LoadResStr("IDS_NET_QUERY_DIRECTJOIN");
	case NRQT_Unknown:
		assert(!"Unknown QueryType");
		return "";
	};
	return "";
}

void C4StartupNetListEntry::SetRefQuery(const char *szAddress, enum QueryType eQueryType)
{
	// safety: clear previous
	ClearRef();
	// setup layout
	const_cast<C4Facet &>(static_cast<const C4Facet &>(pIcon->GetFacet())) = static_cast<const C4Facet &>(C4Startup::Get()->Graphics.fctNetGetRef);
	pIcon->SetAnimated(true, 1);
	pIcon->SetBounds(rctIconLarge);
	// init a new ref client to query
	sRefClientAddress.Copy(szAddress);
	this->eQueryType = eQueryType;
	pRefClient = new C4Network2RefClient();
	if (!pRefClient->Init() || !pRefClient->SetServer(szAddress))
	{
		// should not happen
		sInfoText[0].Clear();
		SetError(pRefClient->GetError(), TT_RefReqWait);
		return;
	}
	// set info
	sInfoText[0].Format(LoadResStr("IDS_NET_CLIENTONNET"), GetQueryTypeName(eQueryType), pRefClient->getServerName());
	sInfoText[1].Copy(LoadResStr("IDS_NET_INFOQUERY"));
	UpdateSmallState(); UpdateText();
	pRefClient->SetNotify(&Application.InteractiveThread);
	// masterserver: always on top
	if (eQueryType == NRQT_Masterserver)
		iSortOrder = 100;
	// register proc
	C4InteractiveThread &Thread = Application.InteractiveThread;
	Thread.AddProc(pRefClient);
	// start querying!
	QueryReferences();
}

bool C4StartupNetListEntry::QueryReferences()
{
	// begin querying
	if (!pRefClient->QueryReferences())
	{
		SetError(pRefClient->GetError(), TT_RefReqWait);
		return false;
	}
	// set up timeout
	iRequestTimeout = time(nullptr) + C4NetRefRequestTimeout;
	return true;
}

bool C4StartupNetListEntry::Execute()
{
	// update entries
	// if the return value is false, this entry will be deleted
	// timer running?
	if (iTimeout) if (time(nullptr) >= iTimeout)
	{
		// timeout!
		// for internet servers, this means refresh needed - search anew!
		if (pRefClient && eQueryType == NRQT_Masterserver)
		{
			fError = false;
			sError.Clear();
			const_cast<C4Facet &>(static_cast<const C4Facet &>(pIcon->GetFacet())) = static_cast<const C4Facet &>(C4Startup::Get()->Graphics.fctNetGetRef);
			pIcon->SetAnimated(true, 1);
			pIcon->SetBounds(rctIconLarge);
			sInfoText[1].Copy(LoadResStr("IDS_NET_INFOQUERY"));
			iTimeout = 0;
			QueryReferences();
			// always keep item even if query failed
			return true;
		}
		// any other item is just removed - return value marks this
		return false;
	}
	// failed without a timer. Nothing to be done about it.
	if (fError) { OnRequestFailed(); return true; }
	// updates need to be done for references being retrieved only
	if (!pRefClient) return true;
	// check if it has arrived
	if (pRefClient->isBusy())
		// still requesting - but do not wait forever
		if (time(nullptr) >= iRequestTimeout)
		{
			SetError(LoadResStr("IDS_NET_ERR_REFREQTIMEOUT"), TT_RefReqWait);
			pRefClient->Cancel("Timeout");
			OnRequestFailed();
		}
	return true;
}

void C4StartupNetListEntry::OnRequestFailed()
{
	++iNumFails;
	// special clonk.de handling: Alternate between ports 80 and 84 and re-request direcly (since we've already waited for the timeout)
	if (iNumFails <= 2 && eQueryType == NRQT_Masterserver)
	{
		if (sRefClientAddress == C4CFG_LeagueServer)
			SetRefQuery(C4CFG_FallbackServer, NRQT_Masterserver);
		else if (sRefClientAddress == C4CFG_FallbackServer)
			SetRefQuery(C4CFG_LeagueServer, NRQT_Masterserver);
	}
}

bool C4StartupNetListEntry::OnReference()
{
	// wrong type / still busy?
	if (!pRefClient || pRefClient->isBusy())
		return true;
	// successful?
	if (!pRefClient->isSuccess())
	{
		// couldn't get references
		SetError(pRefClient->GetError(), TT_RefReqWait);
		return true;
	}
	// Ref getting done!
	pIcon->SetAnimated(false, 1);
	// Get reference information from client
	C4Network2Reference **ppNewRefs = nullptr; int32_t iNewRefCount;
	if (!pRefClient->GetReferences(ppNewRefs, iNewRefCount))
	{
		// References could be retrieved but not read
		SetError(LoadResStr("IDS_NET_ERR_REFINVALID"), TT_RefReqWait);
		delete[] ppNewRefs;
		return true;
	}
	if (!iNewRefCount)
	{
		// References retrieved but no game open: Inform user
		sInfoText[1].Copy(LoadResStr("IDS_NET_INFONOGAME"));
		UpdateText();
	}
	else
	{
		// Grab references, count players
		C4StartupNetListEntry *pNewRefEntry = this; int iPlayerCount = 0;
		for (int i = 0; i < iNewRefCount; i++)
		{
			pNewRefEntry = AddReference(ppNewRefs[i], pNewRefEntry->GetNextLower(ppNewRefs[i]->getSortOrder()));
			iPlayerCount += ppNewRefs[i]->Parameters.PlayerInfos.GetActivePlayerCount(false);
		}
		// Update text accordingly
		sInfoText[1].Format(LoadResStr("IDS_NET_INFOGAMES"), static_cast<int>(iNewRefCount), iPlayerCount);
		UpdateText();
	}
	delete[] ppNewRefs;
	// special masterserver handling
	if (eQueryType == NRQT_Masterserver)
	{
		// show message of the day, if any
		int32_t iMasterServerMessages = 0;
		if (pRefClient->GetMessageOfTheDay() && *pRefClient->GetMessageOfTheDay())
			sInfoText[1 + ++iMasterServerMessages].Format(LoadResStr("IDS_NET_MOTD"), pRefClient->GetMessageOfTheDay());
		const char *szMotDLink = pRefClient->GetMessageOfTheDayHyperlink();
		if (szMotDLink && *szMotDLink)
		{
			sInfoText[1 + ++iMasterServerMessages].Copy(szMotDLink);
			iInfoLink = 1 + iMasterServerMessages;
		}
		if (iMasterServerMessages)
		{
			UpdateSmallState();
			UpdateText();
		}
		// Check if the server has delivered a redirect from itself
		// (alternate servers may not redirect)
		if (pRefClient->GetLeagueServerRedirect() && (!Config.Network.UseAlternateServer || SEqual(Config.Network.ServerAddress, Config.Network.AlternateServerAddress)) && !pNetDlg->GetIgnoreUpdate())
		{
			const char *newLeagueServer = pRefClient->GetLeagueServerRedirect();
			if (newLeagueServer && !SEqual(newLeagueServer, Config.Network.ServerAddress))
			{
				// this is a new redirect. Inform the user and auto-change servers if desired
				StdStrBuf sMessage;
				sMessage.Format(LoadResStr("IDS_NET_SERVERREDIRECTMSG"), newLeagueServer);
				if (GetScreen()->ShowMessageModal(sMessage.getData(), LoadResStr("IDS_NET_SERVERREDIRECT"), C4GUI::MessageDialog::btnYesNo, C4GUI::Ico_OfficialServer))
				{
					// apply new server setting
					SCopy(newLeagueServer, Config.Network.ServerAddress, CFG_MaxString);
					Config.Save();
					GetScreen()->ShowMessageModal(LoadResStr("IDS_NET_SERVERREDIRECTDONE"), LoadResStr("IDS_NET_SERVERREDIRECT"), C4GUI::MessageDialog::btnOK, C4GUI::Ico_OfficialServer);
					SetTimeout(TT_Refresh);
					return true;
				}
				else
				{
					pNetDlg->SetIgnoreUpdate(true);
				}
			}
		}
		// masterserver: schedule next query
		SetTimeout(TT_Masterserver);
		return true;
	}
	// non-masterserver
	if (iNewRefCount)
	{
		// this item has been "converted" into the references - remove without further feedback
		return false;
	}
	else
		// no ref found on custom adress: Schedule re-check
		SetTimeout(TT_RefReqWait);
	return true;
}

C4GUI::Element *C4StartupNetListEntry::GetNextLower(int32_t sortOrder)
{
	// search list for the next element of a lower sort order
	for (C4GUI::Element *pElem = pList->GetFirst(); pElem; pElem = pElem->GetNext())
	{
		C4StartupNetListEntry *pEntry = static_cast<C4StartupNetListEntry *>(pElem);
		if (pEntry->iSortOrder < sortOrder)
			return pElem;
	}
	// none found: insert at start
	return nullptr;
}

void C4StartupNetListEntry::UpdateCollapsed(bool fToCollapseValue)
{
	// if collapsed state changed, update the text
	if (fIsCollapsed == fToCollapseValue) return;
	fIsCollapsed = fToCollapseValue;
	UpdateSmallState();
}

void C4StartupNetListEntry::UpdateSmallState()
{
	// small view: Always collapsed if there is no extended text
	bool fNewIsSmall = !sInfoText[2].getLength() || fIsCollapsed;
	if (fNewIsSmall == fIsSmall) return;
	fIsSmall = fNewIsSmall;
	for (int i = 2; i < InfoLabelCount; ++i) pInfoLbl[i]->SetVisibility(!fIsSmall);
	UpdateEntrySize();
}

void C4StartupNetListEntry::UpdateEntrySize()
{
	// restack all labels by their size
	int32_t iLblCnt = (fIsSmall ? 2 : InfoLabelCount), iY = 1;
	while (iLblCnt > 2 && !sInfoText[iLblCnt - 1])
		iLblCnt--;
	for (int i = 0; i < iLblCnt; ++i)
	{
		C4Rect rcBounds = pInfoLbl[i]->GetBounds();
		rcBounds.y = iY;
		iY += rcBounds.Hgt + 2;
		pInfoLbl[i]->SetBounds(rcBounds);
	}
	// resize this control
	GetBounds().Hgt = iY - 1;
	UpdateSize();
}

void C4StartupNetListEntry::UpdateText()
{
	bool fRestackElements = false;
	CStdFont *pUseFont = &(C4GUI::GetRes()->TextFont);
	// adjust icons
	int32_t sx = iInfoIconCount * pUseFont->GetLineHeight();
	int32_t i;
	for (i = iInfoIconCount; i < MaxInfoIconCount; ++i)
	{
		pInfoIcons[i]->SetIcon(C4GUI::Ico_None);
		pInfoIcons[i]->SetToolTip(nullptr);
	}
	// text to labels
	for (i = 0; i < InfoLabelCount; ++i)
	{
		int iAvailableWdt = GetClientRect().Wdt - pInfoLbl[i]->GetBounds().x - 1;
		if (!i) iAvailableWdt -= sx;
		StdStrBuf BrokenText;
		pUseFont->BreakMessage(sInfoText[i].getData(), iAvailableWdt, &BrokenText, true);
		int32_t iHgt, iWdt;
		if (pUseFont->GetTextExtent(BrokenText.getData(), iWdt, iHgt, true))
		{
			if ((pInfoLbl[i]->GetBounds().Hgt != iHgt) || (pInfoLbl[i]->GetBounds().Wdt != iAvailableWdt))
			{
				C4Rect rcBounds = pInfoLbl[i]->GetBounds();
				rcBounds.Wdt = iAvailableWdt;
				rcBounds.Hgt = iHgt;
				pInfoLbl[i]->SetBounds(rcBounds);
				fRestackElements = true;
			}
		}
		pInfoLbl[i]->SetText(BrokenText.getData());
		if (iInfoLink == i)
			pInfoLbl[i]->SetHyperlink(sInfoText[i].getData());
		else
			pInfoLbl[i]->SetColor(fIsEnabled ? C4GUI_MessageFontClr : C4GUI_InactMessageFontClr);
		pInfoLbl[i]->SetVisibility(BrokenText.getLength());
	}
	if (fRestackElements) UpdateEntrySize();
}

void C4StartupNetListEntry::AddStatusIcon(C4GUI::Icons eIcon, const char *szToolTip)
{
	// safety
	if (iInfoIconCount == MaxInfoIconCount) return;
	// set icon to the left of the existing icons to the desired data
	pInfoIcons[iInfoIconCount]->SetIcon(eIcon);
	pInfoIcons[iInfoIconCount]->SetToolTip(szToolTip);
	++iInfoIconCount;
}

void C4StartupNetListEntry::SetReference(C4Network2Reference *pRef)
{
	// safety: clear previous
	ClearRef();
	// set info
	this->pRef = pRef;
	int32_t iIcon = pRef->getIcon();
	if (!Inside<int32_t>(iIcon, 0, C4StartupScenSel_IconCount - 1)) iIcon = C4StartupScenSel_DefaultIcon_Scenario;
	pIcon->SetFacet(C4Startup::Get()->Graphics.fctScenSelIcons.GetPhase(iIcon));
	pIcon->SetAnimated(false, 0);
	pIcon->SetBounds(rctIconSmall);
	int32_t iPlrCnt = pRef->Parameters.PlayerInfos.GetActivePlayerCount(false);
	C4Client *pHost = pRef->Parameters.Clients.getHost();
	sInfoText[0].Format(LoadResStr("IDS_NET_REFONCLIENT"), pRef->getTitle(), pHost ? pHost->getName() : "unknown");
	sInfoText[1].Format(LoadResStr("IDS_NET_INFOPLRSGOALDESC"),
		static_cast<int>(iPlrCnt),
		static_cast<int>(pRef->Parameters.MaxPlayers),
		pRef->Parameters.GetGameGoalString().getData(),
		StdStrBuf(pRef->getGameStatus().getDescription(), true).getData());
	if (pRef->getTime() > 0)
	{
		StdStrBuf strDuration; strDuration.Format("%02d:%02d:%02d", pRef->getTime() / 3600, (pRef->getTime() % 3600) / 60, pRef->getTime() % 60);
		sInfoText[1].Append(" - "); sInfoText[1].Append(strDuration);
	}
	sInfoText[2].Format(LoadResStr("IDS_DESC_VERSION"), pRef->getGameVersion().GetString().getData());
	sInfoText[3].Format("%s: %s", LoadResStr("IDS_CTL_COMMENT"), pRef->getComment());
	// password
	if (pRef->isPasswordNeeded())
		AddStatusIcon(C4GUI::Ico_Ex_LockedFrontal, LoadResStr("IDS_NET_INFOPASSWORD"));
	// league
	if (pRef->Parameters.isLeague())
		AddStatusIcon(C4GUI::Ico_Ex_League, pRef->Parameters.getLeague());
	// lobby active
	if (pRef->getGameStatus().isLobbyActive())
		AddStatusIcon(C4GUI::Ico_Lobby, LoadResStr("IDS_DESC_EXPECTING"));
	// game running
	if (pRef->getGameStatus().isPastLobby())
		AddStatusIcon(C4GUI::Ico_GameRunning, LoadResStr("IDS_NET_INFOINPROGR"));
	// runtime join
	if (pRef->isJoinAllowed() && pRef->getGameStatus().isPastLobby()) // A little workaround to determine RuntimeJoin...
		AddStatusIcon(C4GUI::Ico_RuntimeJoin, LoadResStr("IDS_NET_RUNTIMEJOINFREE"));
	// fair crew
	if (pRef->Parameters.UseFairCrew)
		AddStatusIcon(C4GUI::Ico_Ex_FairCrew, LoadResStr("IDS_CTL_FAIRCREW_DESC"));
	// official server
	if (pRef->isOfficialServer() && !Config.Network.UseAlternateServer) // Offical server icon is only displayed if references are obtained from official league server
	{
		fIsImportant = true;
		AddStatusIcon(C4GUI::Ico_OfficialServer, LoadResStr("IDS_NET_OFFICIALSERVER"));
	}
	// list participating player names
	sInfoText[4].Format("%s: %s", LoadResStr("IDS_CTL_PLAYER"), iPlrCnt ? pRef->Parameters.PlayerInfos.GetActivePlayerNames(false).getData() : LoadResStr("IDS_CTL_NONE"));
	// disabled if join is not possible for some reason
	C4GameVersion verThis;
	if (!pRef->isJoinAllowed() || !(pRef->getGameVersion() == verThis))
	{
		fIsEnabled = false;
	}
	// store sort order
	iSortOrder = pRef->getSortOrder();
	// all references expire after a while
	SetTimeout(TT_Reference);
	UpdateSmallState(); UpdateText();
}

void C4StartupNetListEntry::SetError(const char *szErrorText, TimeoutType eTimeout)
{
	// set error message
	fError = true;
	sInfoText[1].Copy(szErrorText);
	for (int i = 2; i < InfoLabelCount; ++i) sInfoText[i].Clear();
	InvalidateStatusIcons();
	UpdateSmallState(); UpdateText();
	pIcon->SetIcon(C4GUI::Ico_Close);
	pIcon->SetAnimated(false, 0);
	pIcon->SetBounds(rctIconSmall);
	SetTimeout(eTimeout);
}

void C4StartupNetListEntry::SetTimeout(TimeoutType eTimeout)
{
	int iTime = 0;
	switch (eTimeout)
	{
	case TT_RefReqWait: iTime = (eQueryType == NRQT_Masterserver) ? C4NetMasterServerQueryInterval : C4NetErrorRefTimeout; break;
	case TT_Reference: iTime = C4NetReferenceTimeout; break;
	case TT_Masterserver: iTime = C4NetMasterServerQueryInterval; break;
	case TT_Refresh: iTime = 1; break; // refresh ASAP
	};
	if (!iTime) return;
	iTimeout = time(nullptr) + iTime;
}

C4StartupNetListEntry *C4StartupNetListEntry::AddReference(C4Network2Reference *pAddRef, C4GUI::Element *pInsertBefore)
{
	// check list whether the same reference has been added already
	for (C4GUI::Element *pElem = pList->GetFirst(); pElem; pElem = pElem->GetNext())
	{
		C4StartupNetListEntry *pEntry = static_cast<C4StartupNetListEntry *>(pElem);
		// match to existing reference entry:
		// * same host (checking for same name and nick)
		// * at least one match in address and port
		// * the incoming reference is newer than (or same as) the current one
		if (pEntry->IsSameHost(pAddRef)
			&& pEntry->IsSameAddress(pAddRef)
			&& (pEntry->GetReference()->getStartTime() <= pAddRef->getStartTime()))
		{
			// update existing entry
			pEntry->SetReference(pAddRef);
			return pEntry;
		}
	}
	// no update - just add
	C4StartupNetListEntry *pNewRefEntry = new C4StartupNetListEntry(pList, pInsertBefore, pNetDlg);
	pNewRefEntry->SetReference(pAddRef);
	pNetDlg->OnReferenceEntryAdd(pNewRefEntry);
	return pNewRefEntry;
}

bool C4StartupNetListEntry::IsSameHost(const C4Network2Reference *pRef2)
{
	// not if ref has not been retrieved yet
	if (!pRef) return false;
	C4Client *pHost1 = pRef->Parameters.Clients.getHost();
	C4Client *pHost2 = pRef2->Parameters.Clients.getHost();
	if (!pHost1 || !pHost2) return false;
	// check
	return SEqual(pHost1->getName(), pHost2->getName());
}

bool C4StartupNetListEntry::IsSameAddress(const C4Network2Reference *pRef2)
{
	// not if ref has not been retrieved yet
	if (!pRef) return false;
	// check all of our addresses
	for (int i = 0; i < pRef->getAddrCnt(); i++)
		// against all of the other ref's addresses
		for (int j = 0; j < pRef2->getAddrCnt(); j++)
			// at least one match!
			if (pRef->getAddr(i) == pRef2->getAddr(j))
				return true;
	// no match
	return false;
}

bool C4StartupNetListEntry::IsSameRefQueryAddress(const char *szJoinaddress)
{
	// only unretrieved references
	if (!pRefClient) return false;
	// if request failed, create a duplicate anyway in case the game is opened now
	// except masterservers, which would re-search some time later anyway
	if (fError && eQueryType != NRQT_Masterserver) return false;
	// check equality of address
	// do it the simple way for now
	return SEqualNoCase(sRefClientAddress.getData(), szJoinaddress);
}

const char *C4StartupNetListEntry::GetJoinAddress()
{
	// only unresolved references
	if (!pRefClient) return nullptr;
	// not masterservers (cannot join directly on clonk.de)
	if (eQueryType == NRQT_Masterserver) return nullptr;
	// return join address
	return pRefClient->getServerName();
}

C4Network2Reference *C4StartupNetListEntry::GrabReference()
{
	C4Network2Reference *pOldRef = pRef;
	pRef = nullptr;
	return pOldRef;
}

// C4StartupNetDlg

C4StartupNetDlg::C4StartupNetDlg() : C4StartupDlg(LoadResStr("IDS_DLG_NETSTART")), iGameDiscoverInterval(0), pMasterserverClient(nullptr), fIsCollapsed(false), fUpdatingList(false), tLastRefresh(0), pChatTitleLabel(nullptr), fIgnoreUpdate(false)
{
	// key bindings
	C4CustomKey::CodeList keys;
	keys.push_back(C4KeyCodeEx(K_BACK)); keys.push_back(C4KeyCodeEx(K_LEFT));
	pKeyBack = new C4KeyBinding(keys, "StartupNetBack", KEYSCOPE_Gui,
		new C4GUI::DlgKeyCB<C4StartupNetDlg>(*this, &C4StartupNetDlg::KeyBack), C4CustomKey::PRIO_Dlg);
	pKeyRefresh = new C4KeyBinding(C4KeyCodeEx(K_F5), "StartupNetReload", KEYSCOPE_Gui,
		new C4GUI::DlgKeyCB<C4StartupNetDlg>(*this, &C4StartupNetDlg::KeyRefresh), C4CustomKey::PRIO_CtrlOverride);

	// screen calculations
	UpdateSize();
	int32_t iIconSize = C4GUI_IconExWdt;
	int32_t iButtonWidth, iCaptionFontHgt, iSideSize = std::max<int32_t>(GetBounds().Wdt / 6, iIconSize);
	int32_t iButtonHeight = C4GUI_ButtonHgt, iButtonIndent = GetBounds().Wdt / 40;
	C4GUI::GetRes()->CaptionFont.GetTextExtent("<< BACK", iButtonWidth, iCaptionFontHgt, true);
	iButtonWidth *= 3;
	C4GUI::ComponentAligner caMain(GetClientRect(), 0, 0, true);
	C4GUI::ComponentAligner caButtonArea(caMain.GetFromBottom(caMain.GetHeight() / 7), 0, 0);
	int32_t iButtonAreaWdt = caButtonArea.GetWidth() * 7 / 8;
	iButtonWidth = std::min<int32_t>(iButtonWidth, (iButtonAreaWdt - 8 * iButtonIndent) / 4);
	iButtonIndent = (iButtonAreaWdt - 4 * iButtonWidth) / 8;
	C4GUI::ComponentAligner caButtons(caButtonArea.GetCentered(iButtonAreaWdt, iButtonHeight), iButtonIndent, 0);
	C4GUI::ComponentAligner caLeftBtnArea(caMain.GetFromLeft(iSideSize), std::min<int32_t>(caMain.GetWidth() / 20, (iSideSize - C4GUI_IconExWdt) / 2), caMain.GetHeight() / 40);
	C4GUI::ComponentAligner caConfigArea(caMain.GetFromRight(iSideSize), std::min<int32_t>(caMain.GetWidth() / 20, (iSideSize - C4GUI_IconExWdt) / 2), caMain.GetHeight() / 40);

	// left button area: Switch between chat and game list
	if (C4ChatDlg::IsChatEnabled())
	{
		btnGameList = new C4GUI::CallbackButton<C4StartupNetDlg, C4GUI::IconButton>(C4GUI::Ico_Ex_GameList, caLeftBtnArea.GetFromTop(iIconSize, iIconSize), '\0', &C4StartupNetDlg::OnBtnGameList);
		btnGameList->SetToolTip(LoadResStr("IDS_DESC_SHOWSAVAILABLENETWORKGAME"));
		btnGameList->SetText(LoadResStr("IDS_BTN_GAMES"));
		AddElement(btnGameList);
		btnChat = new C4GUI::CallbackButton<C4StartupNetDlg, C4GUI::IconButton>(C4GUI::Ico_Ex_Chat, caLeftBtnArea.GetFromTop(iIconSize, iIconSize), '\0', &C4StartupNetDlg::OnBtnChat);
		btnChat->SetToolTip(LoadResStr("IDS_DESC_CONNECTSTOANIRCCHATSERVER"));
		btnChat->SetText(LoadResStr("IDS_BTN_CHAT"));
		AddElement(btnChat);
	}
	else btnChat = nullptr;

	// main area: Tabular to switch between game list and chat
	pMainTabular = new C4GUI::Tabular(caMain.GetAll(), C4GUI::Tabular::tbNone);
	pMainTabular->SetDrawDecoration(false);
	pMainTabular->SetSheetMargin(0);
	AddElement(pMainTabular);

	// main area: game selection sheet
	C4GUI::Tabular::Sheet *pSheetGameList = pMainTabular->AddSheet(nullptr);
	C4GUI::ComponentAligner caGameList(pSheetGameList->GetContainedClientRect(), 0, 0, false);
	C4GUI::WoodenLabel *pGameListLbl; int32_t iCaptHgt = C4GUI::WoodenLabel::GetDefaultHeight(&C4GUI::GetRes()->TextFont);
	pGameListLbl = new C4GUI::WoodenLabel(LoadResStr("IDS_NET_GAMELIST"), caGameList.GetFromTop(iCaptHgt), C4GUI_Caption2FontClr, &C4GUI::GetRes()->TextFont, ALeft);
	pSheetGameList->AddElement(pGameListLbl);
	pGameSelList = new C4GUI::ListBox(caGameList.GetFromTop(caGameList.GetHeight() - iCaptHgt));
	pGameSelList->SetDecoration(true, nullptr, true, true);
	pGameSelList->UpdateElementPositions();
	pGameSelList->SetSelectionDblClickFn(new C4GUI::CallbackHandler<C4StartupNetDlg>(this, &C4StartupNetDlg::OnSelDblClick));
	pGameSelList->SetSelectionChangeCallbackFn(new C4GUI::CallbackHandler<C4StartupNetDlg>(this, &C4StartupNetDlg::OnSelChange));
	pSheetGameList->AddElement(pGameSelList);
	C4GUI::ComponentAligner caIP(caGameList.GetAll(), 0, 0);
	C4GUI::WoodenLabel *pIPLbl;
	const char *szIPLblText = LoadResStr("IDS_NET_IP");
	int32_t iIPWdt = 100, Q;
	C4GUI::GetRes()->TextFont.GetTextExtent(szIPLblText, iIPWdt, Q, true);
	pIPLbl = new C4GUI::WoodenLabel(szIPLblText, caIP.GetFromLeft(iIPWdt + 10), C4GUI_Caption2FontClr, &C4GUI::GetRes()->TextFont);
	const char *szIPTip = LoadResStr("IDS_NET_IP_DESC");
	pIPLbl->SetToolTip(szIPTip);
	pSheetGameList->AddElement(pIPLbl);
	pJoinAddressEdt = new C4GUI::CallbackEdit<C4StartupNetDlg>(caIP.GetAll(), this, &C4StartupNetDlg::OnJoinAddressEnter);
	pJoinAddressEdt->SetToolTip(szIPTip);
	pSheetGameList->AddElement(pJoinAddressEdt);

	// main area: chat sheet
	if (C4ChatDlg::IsChatEnabled())
	{
		C4GUI::Tabular::Sheet *pSheetChat = pMainTabular->AddSheet(nullptr);
		C4GUI::ComponentAligner caChat(pSheetChat->GetContainedClientRect(), 0, 0, false);
		pSheetChat->AddElement(pChatTitleLabel = new C4GUI::WoodenLabel("", caChat.GetFromTop(iCaptHgt), C4GUI_Caption2FontClr, &C4GUI::GetRes()->TextFont, ALeft, false));
		C4GUI::GroupBox *pChatGroup = new C4GUI::GroupBox(caChat.GetAll());
		pChatGroup->SetColors(0u, C4GUI_CaptionFontClr, C4GUI_StandardBGColor);
		pChatGroup->SetMargin(2);
		pSheetChat->AddElement(pChatGroup);
		pChatCtrl = new C4ChatControl(&Application.IRCClient);
		pChatCtrl->SetBounds(pChatGroup->GetContainedClientRect());
		pChatCtrl->SetTitleChangeCB(new C4GUI::InputCallback<C4StartupNetDlg>(this, &C4StartupNetDlg::OnChatTitleChange));
		StdStrBuf sCurrTitle; sCurrTitle.Ref(pChatCtrl->GetTitle()); OnChatTitleChange(sCurrTitle);
		pChatGroup->AddElement(pChatCtrl);
	}

	// config area
	btnInternet = new C4GUI::CallbackButton<C4StartupNetDlg, C4GUI::IconButton>(Config.Network.MasterServerSignUp ? C4GUI::Ico_Ex_InternetOn : C4GUI::Ico_Ex_InternetOff, caConfigArea.GetFromTop(iIconSize, iIconSize), '\0', &C4StartupNetDlg::OnBtnInternet);
	btnInternet->SetToolTip(LoadResStr("IDS_DLGTIP_SEARCHINTERNETGAME"));
	btnInternet->SetText(LoadResStr("IDS_CTL_INETSERVER"));
	AddElement(btnInternet);
	btnRecord = new C4GUI::CallbackButton<C4StartupNetDlg, C4GUI::IconButton>(Config.General.Record ? C4GUI::Ico_Ex_RecordOn : C4GUI::Ico_Ex_RecordOff, caConfigArea.GetFromTop(iIconSize, iIconSize), '\0', &C4StartupNetDlg::OnBtnRecord);
	btnRecord->SetToolTip(LoadResStr("IDS_DLGTIP_RECORD"));
	btnRecord->SetText(LoadResStr("IDS_CTL_RECORD"));
	AddElement(btnRecord);

	// button area
	C4GUI::CallbackButton<C4StartupNetDlg> *btn;
	AddElement(btn = new C4GUI::CallbackButton<C4StartupNetDlg>(LoadResStr("IDS_BTN_BACK"), caButtons.GetFromLeft(iButtonWidth), &C4StartupNetDlg::OnBackBtn));
	btn->SetToolTip(LoadResStr("IDS_DLGTIP_BACKMAIN"));
	AddElement(btnRefresh = new C4GUI::CallbackButton<C4StartupNetDlg>(LoadResStr("IDS_BTN_RELOAD"), caButtons.GetFromLeft(iButtonWidth), &C4StartupNetDlg::OnRefreshBtn));
	btnRefresh->SetToolTip(LoadResStr("IDS_NET_RELOAD_DESC"));
	AddElement(btnJoin = new C4GUI::CallbackButton<C4StartupNetDlg>(LoadResStr("IDS_NET_JOINGAME_BTN"), caButtons.GetFromLeft(iButtonWidth), &C4StartupNetDlg::OnJoinGameBtn));
	btnJoin->SetToolTip(LoadResStr("IDS_NET_JOINGAME_DESC"));
	AddElement(btn = new C4GUI::CallbackButton<C4StartupNetDlg>(LoadResStr("IDS_NET_NEWGAME"), caButtons.GetFromLeft(iButtonWidth), &C4StartupNetDlg::OnCreateGameBtn));
	btn->SetToolTip(LoadResStr("IDS_NET_NEWGAME_DESC"));

	// initial dlg mode
	UpdateDlgMode();

	// initial focus
	SetFocus(GetDlgModeFocusControl(), false);

	// initialize discovery
	DiscoverClient.Init(Config.Network.PortDiscovery);
	DiscoverClient.StartDiscovery();
	iGameDiscoverInterval = C4NetGameDiscoveryInterval;

	// create timer
	pSec1Timer = new C4Sec1TimerCallback<C4StartupNetDlg>(this);

	// register as receiver of reference notifies
	Application.InteractiveThread.SetCallback(Ev_HTTP_Response, this);
}

C4StartupNetDlg::~C4StartupNetDlg()
{
	// disable notifies
	Application.InteractiveThread.ClearCallback(Ev_HTTP_Response, this);
	DiscoverClient.Close();
	pSec1Timer->Release();
	delete pMasterserverClient;

	delete pKeyBack;
	delete pKeyRefresh;
}

void C4StartupNetDlg::DrawElement(C4FacetEx &cgo)
{
	// draw background
	DrawBackground(cgo, C4Startup::Get()->Graphics.fctNetBG);
}

bool C4StartupNetDlg::IsOpen(C4StartupNetDlg *const instance)
{
	return C4Startup::Get() && C4Startup::Get()->pCurrDlg == instance;
}

void C4StartupNetDlg::OnShown()
{
	// callback when shown: Start searching for games
	fIgnoreUpdate = false;
	C4StartupDlg::OnShown();
	UpdateList();
	UpdateMasterserver();
	OnSec1Timer();
	tLastRefresh = time(nullptr);
	// also update chat
	if (pChatCtrl) pChatCtrl->OnShown();
}

void C4StartupNetDlg::OnClosed(bool fOK)
{
	// dlg abort: return to main screen
	pGameSelList->SelectNone(false);
	delete pMasterserverClient; pMasterserverClient = nullptr;
	if (!fOK) DoBack();
}

C4GUI::Control *C4StartupNetDlg::GetDefaultControl()
{
	// default control depends on whether dlg is in chat or game list mode
	if (GetDlgMode() == SNDM_Chat && pChatCtrl)
		// chat mode: Chat input edit
		return pChatCtrl->GetDefaultControl();
	else
		// game list mode: No default control, because it would move focus away from IP input edit
		return nullptr;
}

C4GUI::Control *C4StartupNetDlg::GetDlgModeFocusControl()
{
	// default control depends on whether dlg is in chat or game list mode
	if (GetDlgMode() == SNDM_Chat && pChatCtrl)
		// chat mode: Chat input edit
		return pChatCtrl->GetDefaultControl();
	else
		// game list mode: Game list box
		return pGameSelList;
}

void C4StartupNetDlg::OnBtnGameList(C4GUI::Control *btn)
{
	// switch to game list dialog
	pMainTabular->SelectSheet(SNDM_GameList, true);
	UpdateDlgMode();
}

void C4StartupNetDlg::OnBtnChat(C4GUI::Control *btn)
{
	// toggle chat / game list
	if (pChatCtrl)
		if (pMainTabular->GetActiveSheetIndex() == SNDM_GameList)
		{
			pMainTabular->SelectSheet(SNDM_Chat, true);
			pChatCtrl->OnShown();
			UpdateDlgMode();
		}
		else
		{
			pMainTabular->SelectSheet(SNDM_GameList, true);
			UpdateDlgMode();
		}
}

void C4StartupNetDlg::OnBtnInternet(C4GUI::Control *btn)
{
	// toggle masterserver game search
	Config.Network.MasterServerSignUp = !Config.Network.MasterServerSignUp;
	UpdateMasterserver();
}

void C4StartupNetDlg::OnBtnRecord(C4GUI::Control *btn)
{
	// toggle league signup flag
	btnRecord->SetIcon((Config.General.Record = !Config.General.Record) ? C4GUI::Ico_Ex_RecordOn : C4GUI::Ico_Ex_RecordOff);
}

void C4StartupNetDlg::UpdateMasterserver()
{
	// update button icon to current state
	btnInternet->SetIcon(Config.Network.MasterServerSignUp ? C4GUI::Ico_Ex_InternetOn : C4GUI::Ico_Ex_InternetOff);
	// creates masterserver object if masterserver is enabled; destroy otherwise
	if (!Config.Network.MasterServerSignUp == !pMasterserverClient) return;
	if (!Config.Network.MasterServerSignUp)
	{
		delete pMasterserverClient;
		pMasterserverClient = nullptr;
	}
	else
	{
		pMasterserverClient = new C4StartupNetListEntry(pGameSelList, nullptr, this);
		pMasterserverClient->SetRefQuery(Config.Network.GetLeagueServerAddress(), C4StartupNetListEntry::NRQT_Masterserver);
	}
}

void C4StartupNetDlg::UpdateList(bool fGotReference)
{
	// recursion check
	if (fUpdatingList) return;
	fUpdatingList = true;
	pGameSelList->FreezeScrolling();
	// Update all child entries
	bool fAnyRemoval = false;
	C4GUI::Element *pElem, *pNextElem = pGameSelList->GetFirst();
	while (pElem = pNextElem)
	{
		pNextElem = pElem->GetNext(); // determine next exec element now - execution
		C4StartupNetListEntry *pEntry = static_cast<C4StartupNetListEntry *>(pElem);
		// do item updates
		bool fKeepEntry = true;
		if (fGotReference)
			fKeepEntry = pEntry->OnReference();
		if (fKeepEntry)
			fKeepEntry = pEntry->Execute();
		// remove?
		if (!fKeepEntry)
		{
			// entry wishes to be removed
			// if the selected entry is being removed, the next entry should be selected (which might be the ref for a finished refquery)
			if (pGameSelList->GetSelectedItem() == pEntry)
				if (pEntry->GetNext())
				{
					pGameSelList->SelectEntry(pEntry->GetNext(), false);
				}
			delete pEntry;
			fAnyRemoval = true; // setting any removal will also update collapsed state of all entries; so no need to do updates because of selection change here
		}
	}

	// Add LAN games
	C4NetIO::addr_t Discover;
	while (DiscoverClient.PopDiscover(Discover))
	{
		StdStrBuf Address(Discover.ToString());
		AddReferenceQuery(Address.getData(), C4StartupNetListEntry::NRQT_GameDiscovery);
	}

	// check whether view needs to be collapsed or uncollapsed
	if (fIsCollapsed && fAnyRemoval)
	{
		// try uncollapsing
		fIsCollapsed = false;
		UpdateCollapsed();
		// if scrolling is still necessary, the view will be collapsed again immediately
	}
	if (!fIsCollapsed && pGameSelList->IsScrollingNecessary())
	{
		fIsCollapsed = true;
		UpdateCollapsed();
	}

	fUpdatingList = false;
	// done; selection might have changed
	pGameSelList->UnFreezeScrolling();
	UpdateSelection(false);
}

void C4StartupNetDlg::UpdateCollapsed()
{
	// Uncollapse one element even if list is collapsed. Choose masterserver if no element is currently selected.
	C4GUI::Element *pUncollapseElement = pGameSelList->GetSelectedItem();
	if (!pUncollapseElement) pUncollapseElement = pMasterserverClient;
	// update collapsed state for all child entries
	for (C4GUI::Element *pElem = pGameSelList->GetFirst(); pElem; pElem = pElem->GetNext())
	{
		C4StartupNetListEntry *pEntry = static_cast<C4StartupNetListEntry *>(pElem);
		pEntry->UpdateCollapsed(fIsCollapsed && pElem != pUncollapseElement);
	}
}

void C4StartupNetDlg::UpdateSelection(bool fUpdateCollapsed)
{
	// not during list updates - list update call will do this
	if (fUpdatingList) return;
	// in collapsed view, updating the selection may uncollapse something
	if (fIsCollapsed && fUpdateCollapsed) UpdateCollapsed();
	// 2do: no selection: join button disabled
}

void C4StartupNetDlg::UpdateDlgMode()
{
	DlgMode eMode = GetDlgMode();
	// buttons for game joining only visible in game list mode
	btnInternet->SetVisibility(eMode == SNDM_GameList);
	btnRecord->SetVisibility(eMode == SNDM_GameList);
	btnJoin->SetVisibility(eMode == SNDM_GameList);
	btnRefresh->SetVisibility(eMode == SNDM_GameList);
	// focus update
	if (!GetFocus()) SetFocus(GetDlgModeFocusControl(), false);
}

C4StartupNetDlg::DlgMode C4StartupNetDlg::GetDlgMode()
{
	// dlg mode determined by active tabular sheet
	if (pMainTabular->GetActiveSheetIndex() == SNDM_Chat) return SNDM_Chat; else return SNDM_GameList;
}

void C4StartupNetDlg::OnThreadEvent(C4InteractiveEventType eEvent, const std::any &eventData)
{
	UpdateList(true);
}

bool C4StartupNetDlg::DoOK()
{
	// OK in chat mode? Forward to chat control
	if (GetDlgMode() == SNDM_Chat) return pChatCtrl->DlgEnter();
	// OK on editbox with text enetered: Add the specified IP for reference retrieval
	if (GetFocus() == pJoinAddressEdt)
	{
		const char *szDirectJoinAddress = pJoinAddressEdt->GetText();
		if (szDirectJoinAddress && *szDirectJoinAddress)
		{
			AddReferenceQuery(szDirectJoinAddress, C4StartupNetListEntry::NRQT_DirectJoin);
			// Switch focus to list so another OK joins the specified address
			SetFocus(pGameSelList, true);
			return true;
		}
	}
	// get currently selected item
	C4GUI::Element *pSelection = pGameSelList->GetSelectedItem();
	StdStrBuf strNoJoin(LoadResStr("IDS_NET_NOJOIN"));
	if (!pSelection)
	{
		// no ref selected: Oh noes!
		Game.pGUI->ShowMessageModal(
			LoadResStr("IDS_NET_NOJOIN_NOREF"),
			strNoJoin.getData(),
			C4GUI::MessageDialog::btnOK,
			C4GUI::Ico_Error);
		return true;
	}
	C4StartupNetListEntry *pRefEntry = static_cast<C4StartupNetListEntry *>(pSelection);
	const char *szError;
	if (szError = pRefEntry->GetError())
	{
		// erroneous ref selected: Oh noes!
		Game.pGUI->ShowMessageModal(
			FormatString(LoadResStr("IDS_NET_NOJOIN_BADREF"), szError).getData(),
			strNoJoin.getData(),
			C4GUI::MessageDialog::btnOK,
			C4GUI::Ico_Error);
		return true;
	}
	C4Network2Reference *pRef = pRefEntry->GetReference();
	const char *szDirectJoinAddress = pRefEntry->GetJoinAddress();
	if (!pRef && !(szDirectJoinAddress && *szDirectJoinAddress))
	{
		// something strange has been selected (e.g., a masterserver entry). Error.
		Game.pGUI->ShowMessageModal(
			LoadResStr("IDS_NET_NOJOIN_NOREF"),
			strNoJoin.getData(),
			C4GUI::MessageDialog::btnOK,
			C4GUI::Ico_Error);
		return true;
	}
	// check if join to this reference is possible at all
	if (pRef)
	{
		// version mismatch
		C4GameVersion verThis;
		if (!(pRef->getGameVersion() == verThis))
		{
			Game.pGUI->ShowMessageModal(
				FormatString(LoadResStr("IDS_NET_NOJOIN_BADVER"),
					pRef->getGameVersion().GetString().getData(),
					verThis.GetString().getData()).getData(),
				strNoJoin.getData(),
				C4GUI::MessageDialog::btnOK,
				C4GUI::Ico_Error);
			return true;
		}
		// no runtime join
		if (!pRef->isJoinAllowed())
		{
			if (!Game.pGUI->ShowMessageModal(
				LoadResStr("IDS_NET_NOJOIN_NORUNTIME"),
				strNoJoin.getData(),
				C4GUI::MessageDialog::btnYes | C4GUI::MessageDialog::btnNo,
				C4GUI::Ico_Error))
			{
				return true;
			}
		}
	}
	// OK; joining! Take over reference
	pRefEntry->GrabReference();
	// Set join parameters
	*Game.ScenarioFilename = '\0';
	if (szDirectJoinAddress) SCopy(szDirectJoinAddress, Game.DirectJoinAddress, _MAX_PATH); else *Game.DirectJoinAddress = '\0';
	Game.DefinitionFilenames.push_back("Objects.c4d");
	Game.NetworkActive = true;
	Game.fObserve = false;
	Game.pJoinReference = pRef;
	// start with this set!
	C4Startup::Get()->Start();
	return true;
}

bool C4StartupNetDlg::DoBack()
{
	// abort dialog: Back to main
	C4Startup::Get()->SwitchDialog(C4Startup::SDID_Back);
	return true;
}

void C4StartupNetDlg::DoRefresh()
{
	// check min refresh timer
	time_t tNow = time(nullptr);
	if (tLastRefresh && tNow < tLastRefresh + C4NetMinRefreshInterval)
	{
		// avoid hammering on refresh key
		C4GUI::GUISound("Error");
		return;
	}
	tLastRefresh = tNow;
	// empty list of all old entries
	fUpdatingList = true;
	while (pGameSelList->GetFirst()) delete pGameSelList->GetFirst();
	pMasterserverClient = nullptr;
	// (Re-)Start discovery
	if (!DiscoverClient.StartDiscovery())
	{
		StdStrBuf strNoDiscovery(LoadResStr("IDS_NET_NODISCOVERY"));
		Game.pGUI->ShowMessageModal(
			FormatString(LoadResStr("IDS_NET_NODISCOVERY_DESC"), DiscoverClient.GetError()).getData(),
			strNoDiscovery.getData(),
			C4GUI::MessageDialog::btnAbort,
			C4GUI::Ico_Error);
	}
	iGameDiscoverInterval = C4NetGameDiscoveryInterval;
	// restart masterserver query
	UpdateMasterserver();
	// done; update stuff
	fUpdatingList = false;
	UpdateList();
}

void C4StartupNetDlg::CreateGame()
{
	C4Startup::Get()->SwitchDialog(C4Startup::SDID_ScenSelNetwork);
}

void C4StartupNetDlg::OnSec1Timer()
{
	// no updates if dialog is inactive (e.g., because a join password dlg is shown!)
	if (!IsActive(true))
		return;

	// Execute discovery
	if (!iGameDiscoverInterval--)
	{
		DiscoverClient.StartDiscovery();
		iGameDiscoverInterval = C4NetGameDiscoveryInterval;
	}
	DiscoverClient.Execute(0);

	UpdateList(false);
}

void C4StartupNetDlg::AddReferenceQuery(const char *szAddress, C4StartupNetListEntry::QueryType eQueryType)
{
	// Check for an active reference query to the same address
	for (C4GUI::Element *pElem = pGameSelList->GetFirst(); pElem; pElem = pElem->GetNext())
	{
		C4StartupNetListEntry *pEntry = static_cast<C4StartupNetListEntry *>(pElem);
		// same address
		if (pEntry->IsSameRefQueryAddress(szAddress))
		{
			// nothing to do, xcept maybe select it
			if (eQueryType == C4StartupNetListEntry::NRQT_DirectJoin) pGameSelList->SelectEntry(pEntry, true);
			return;
		}
	}
	// No reference from same host found - create a new entry
	C4StartupNetListEntry *pEntry = new C4StartupNetListEntry(pGameSelList, nullptr, this);
	pEntry->SetRefQuery(szAddress, eQueryType);
	if (eQueryType == C4StartupNetListEntry::NRQT_DirectJoin)
		pGameSelList->SelectEntry(pEntry, true);
	else if (fIsCollapsed)
		pEntry->UpdateCollapsed(true);
}

void C4StartupNetDlg::OnReferenceEntryAdd(C4StartupNetListEntry *pEntry)
{
	// collapse the new entry if desired
	if (fIsCollapsed && pEntry != pGameSelList->GetSelectedItem())
		pEntry->UpdateCollapsed(true);
}

void C4StartupNetDlg::OnChatTitleChange(const StdStrBuf &sNewTitle)
{
	// update label
	if (pChatTitleLabel) pChatTitleLabel->SetText(FormatString("%s - %s", LoadResStr("IDS_DLG_CHAT"), sNewTitle.getData()).getData());
}
