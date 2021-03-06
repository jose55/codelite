#include "callgraph.h"
#include "uisettingsdlg.h"
#include "uicallgraphpanel.h"
#include "toolbaricons.h"
#include <wx/xrc/xmlres.h>
#include <wx/artprov.h>
#include "workspace.h"
#include "string.h"
#include <wx/image.h>
#include "macromanager.h"
#include <wx/bitmap.h>
#include <wx/aboutdlg.h>
#include <wx/datetime.h>

#include <wx/wfstream.h>
#include <wx/txtstrm.h>
#include <wx/sstream.h>
#include <wx/mstream.h>

#include "file_logger.h"
#include <wx/msgdlg.h>

/*!
  * brief Class for include plugin to CodeLite.
  */

#ifndef nil
#define nil 0
#endif

#define myLog(...)  LogFn(wxString::Format(__VA_ARGS__))

CallGraph* thePlugin = NULL;

//Define the plugin entry point
extern "C" EXPORT IPlugin *CreatePlugin(IManager *manager)
{
    if (thePlugin == 0) {
        thePlugin = new CallGraph(manager);
    }
    return thePlugin;
}

wxString wxbuildinfo()
{
    wxString wxbuild(wxVERSION_STRING);

#if defined(__WXMSW__)
    wxbuild << wxT("-Windows");
#elif defined(__UNIX__)
    wxbuild << wxT("-Linux");
#endif

#if wxUSE_UNICODE
    wxbuild << wxT("-Unicode build");
#else
    wxbuild << wxT("-ANSI build");
#endif

    return wxbuild;
}

extern "C" EXPORT PluginInfo GetPluginInfo()
{
    PluginInfo info;
    info.SetAuthor(wxT("Václav Špruček, Michal Bližňák, Tomas Bata University in Zlin, www.fai.utb.cz"));
    info.SetName(wxT("CallGraph"));
    info.SetDescription(_("Create application call graph from profiling information provided by gprof tool."));
    info.SetVersion(wxT("v1.1.0"));
    return info;
}

extern "C" EXPORT int GetPluginInterfaceVersion()
{
    return PLUGIN_INTERFACE_VERSION;
}

//---- CTOR -------------------------------------------------------------------

CallGraph::CallGraph(IManager *manager)
    : IPlugin(manager)
{
    // will be created on-demand
    m_LogFile = nil;

    m_longName = _("Create application call graph from profiling information provided by gprof tool.");
    m_shortName = wxT("CallGraph");

    m_mgr->GetTheApp()->Connect( XRCID("cg_settings"), wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( CallGraph::OnSettings ), NULL, this );
    m_mgr->GetTheApp()->Connect( XRCID("cg_about"), wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( CallGraph::OnAbout ), NULL, this );

    m_mgr->GetTheApp()->Connect( XRCID("cg_show_callgraph"), wxEVT_COMMAND_TOOL_CLICKED, wxCommandEventHandler( CallGraph::OnShowCallGraph ), NULL, this );

    // initialize paths for standard and stored paths for this plugin
    // GetDotPath();
    // GetGprofPath();
}

//---- DTOR -------------------------------------------------------------------

CallGraph::~CallGraph()
{
    m_mgr->GetTheApp()->Disconnect( XRCID("cg_settings"), wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( CallGraph::OnSettings ), NULL, this );
    m_mgr->GetTheApp()->Disconnect( XRCID("cg_about"), wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( CallGraph::OnAbout ), NULL, this );

    m_mgr->GetTheApp()->Disconnect( XRCID("cg_show_callgraph"), wxEVT_COMMAND_TOOL_CLICKED, wxCommandEventHandler( CallGraph::OnShowCallGraph ), NULL, this );

    wxDELETE(m_LogFile);
}

//-----------------------------------------------------------------------------

void	CallGraph::LogFn(wxString s)
{
    return;		// (log disabled)

    FileLogger::Get()->AddLogLine(wxString("> ") + s, FileLogger::Dbg);

    // on-demand log file creation
    if (nil == m_LogFile) {
        wxFileName  cfn(wxGetenv("HOME"), "callgraph.log");
        wxASSERT(cfn.IsOk());

        m_LogFile = new wxFileOutputStream(cfn.GetFullPath());
    }

    wxTextOutputStream	  tos(*m_LogFile);

    tos << s << "\n";

    // cerr ends up in ~/xsesssion-errors ???
    // cout goes nowhere?
    // std::cout << s << "\n";
    ::wxPrintf("%s\n", s);
}

//-----------------------------------------------------------------------------

clToolBar *CallGraph::CreateToolBar(wxWindow *parent)
{
    //Create the toolbar to be used by the plugin
    clToolBar *tb(NULL);

    // First, check that CodeLite allows plugin to register plugins
    if (m_mgr->AllowToolbar()) {
        // Support both toolbars icon size
        int size = m_mgr->GetToolbarIconSize();

        // Allocate new toolbar, which will be freed later by CodeLite
        tb = new clToolBar(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, clTB_DEFAULT_STYLE);

        // Set the toolbar size
        tb->SetToolBitmapSize(wxSize(size, size));

        // Add tools to the plugins toolbar. You must provide 2 sets of icons: 24x24 and 16x16
        BitmapLoader *bmpLoader = m_mgr->GetStdIcons();

        if (size == 24) {
            tb->AddTool(XRCID("cg_show_callgraph"),
                        _("Show call graph"),
                        bmpLoader->LoadBitmap(wxT("callgraph/24/cg")),
                        _("Show call graph for selected/active project"),
                        wxITEM_NORMAL);
        } else {
            tb->AddTool(XRCID("cg_show_callgraph"),
                        _("Show call graph"),
                        bmpLoader->LoadBitmap(wxT("callgraph/16/cg")),
                        _("Show call graph for selected/active project"),
                        wxITEM_NORMAL);
        }
        tb->Realize();
    }
    return tb;
}

//-----------------------------------------------------------------------------

void CallGraph::CreatePluginMenu(wxMenu *pluginsMenu)
{

    // You can use the below code a snippet:
    wxMenu *menu = new wxMenu();
    wxMenuItem *item(NULL);
    item = new wxMenuItem(menu, XRCID("cg_show_callgraph"), _("Show call graph"), _("Show call graph for selected/active project"), wxITEM_NORMAL);
    menu->Append(item);
    menu->AppendSeparator();
    item = new wxMenuItem(menu, XRCID("cg_settings"), _("Settings..."), wxEmptyString, wxITEM_NORMAL);
    menu->Append(item);
    item = new wxMenuItem(menu, XRCID("cg_about"), _("About..."), wxEmptyString, wxITEM_NORMAL);
    menu->Append(item);
    //
    pluginsMenu->Append(wxID_ANY, wxT("Call Graph"), menu);

}

//-----------------------------------------------------------------------------

wxMenu* CallGraph::CreateProjectPopMenu()
{
    wxMenu* menu = new wxMenu();
    wxMenuItem *item(NULL);

    item = new wxMenuItem(menu, XRCID("cg_show_callgraph"), _("Show call graph"), _("Show call graph for selected project"), wxITEM_NORMAL);
    menu->Append(item);

    return menu;
}

//-----------------------------------------------------------------------------

void CallGraph::HookPopupMenu(wxMenu *menu, MenuType type)
{
    if (type == MenuTypeEditor) {
        //TODO::Append items for the editor context menu
    } else if (type == MenuTypeFileExplorer) {
        //TODO::Append items for the file explorer context menu
    } else if (type == MenuTypeFileView_Workspace) {
        //TODO::Append items for the file view / workspace context menu
    } else if (type == MenuTypeFileView_Project) {
        //TODO::Append items for the file view/Project context menu
        if ( !menu->FindItem( XRCID("cg_show_callgraph_popup") ) ) {
            menu->PrependSeparator();
            menu->Prepend( XRCID("cg_show_callgraph_popup"), _("Call Graph"), CreateProjectPopMenu() );
        }
    } else if (type == MenuTypeFileView_Folder) {
        //TODO::Append items for the file view/Virtual folder context menu
    } else if (type == MenuTypeFileView_File) {
        //TODO::Append items for the file view/file context menu
    }
}

//-----------------------------------------------------------------------------

/*void CallGraph::UnHookPopupMenu(wxMenu *menu, MenuType type)
{
	if (type == MenuTypeEditor) {
		//TODO::Unhook items for the editor context menu
	} else if (type == MenuTypeFileExplorer) {
		//TODO::Unhook  items for the file explorer context menu
	} else if (type == MenuTypeFileView_Workspace) {
		//TODO::Unhook  items for the file view / workspace context menu
	} else if (type == MenuTypeFileView_Project) {
		//TODO::Unhook  items for the file view/Project context menu
	} else if (type == MenuTypeFileView_Folder) {
		//TODO::Unhook  items for the file view/Virtual folder context menu
	} else if (type == MenuTypeFileView_File) {
		//TODO::Unhook  items for the file view/file context menu
	}
}*/

//-----------------------------------------------------------------------------

void CallGraph::UnPlug()
{
    //TODO:: perform the unplug action for this plugin
}

//---- About ------------------------------------------------------------------

void CallGraph::OnAbout(wxCommandEvent& event)
{
    //wxString version = wxString::Format(DBE_VERSION);

    wxString desc = _("Create application call graph from profiling information provided by gprof tool.   \n\n"); // zapsat vice info - neco o aplikaci dot
    desc << wxbuildinfo() << wxT("\n\n");

    wxAboutDialogInfo info;
    info.SetName(_("Call Graph"));
    info.SetVersion(_("v1.1.0"));
    info.SetDescription(desc);
    info.SetCopyright(_("2012 (C) Tomas Bata University, Zlin, Czech Republic"));
    info.SetWebSite(_("http://www.fai.utb.cz"));
    info.AddDeveloper(wxT("Václav Špruček"));
    info.AddDeveloper(wxT("Michal Bližňák"));

    wxAboutBox(info);
}

//---- Test wxProcess ---------------------------------------------------------

wxString	CallGraph::LocateApp(const wxString &app_name)
{
    // myLog("LocateApp(\"%s\")", app_name);

    wxProcess	*proc = new wxProcess(wxPROCESS_REDIRECT);

    wxString	cmd = "which " + app_name;

    // Q: HOW BIG IS INTERNAL BUFFER ???
    int	err = wxExecute(cmd, wxEXEC_SYNC, proc);
    // ignore -1 error due to CL signal handler overload

    /*int	pid = proc->GetPid();

    myLog("  wxExecute(\"%s\") returned err %d, had pid %d", cmd, err, pid);
    */

    // get process output
    wxInputStream	*pis = proc->GetInputStream();
    if (!pis || !pis->CanRead()) {
        delete proc;
        return "<ERROR>";
    }

    // read from it
    wxTextInputStream	tis(*pis);

    wxString	out_str = tis.ReadLine();

    delete proc;

    // myLog("  returned \"%s\"", out_str);

    return out_str;
}

//---- Get Gprof Path ---------------------------------------------------------

wxString CallGraph::GetGprofPath()
{
    ConfCallGraph confData;

    m_mgr->GetConfigTool()->ReadObject(wxT("CallGraph"), &confData);

    wxString gprofPath = confData.GetGprofPath();

    if (!gprofPath.IsEmpty())	return gprofPath;

#ifdef __WXMSW__
    return wxEmptyString;
#else
    gprofPath = LocateApp(GPROF_FILENAME_EXE);
#endif

    confData.SetGprofPath(gprofPath);
    m_mgr->GetConfigTool()->WriteObject(wxT("CallGraph"), &confData);
    return gprofPath;
}

//---- Get Dot Path -----------------------------------------------------------

wxString CallGraph::GetDotPath()
{
    ConfCallGraph confData;

    m_mgr->GetConfigTool()->ReadObject(wxT("CallGraph"), &confData);

    wxString dotPath = confData.GetDotPath();

    if (!dotPath.IsEmpty()) return dotPath;

#ifdef __WXMSW__
    // dont annoy with messages on startup
    return wxEmptyString;
#else

    dotPath = LocateApp(DOT_FILENAME_EXE);
#endif

    confData.SetDotPath(dotPath);

    m_mgr->GetConfigTool()->WriteObject(wxT("CallGraph"), &confData);

    return dotPath;
}

//---- Show CallGraph event ---------------------------------------------------

void CallGraph::OnShowCallGraph(wxCommandEvent& event)
{
    // myLog("wxThread::IsMain(%d)", (int)wxThread::IsMain());

    IConfigTool *config_tool = m_mgr->GetConfigTool();

    config_tool->ReadObject(wxT("CallGraph"), &confData);

    if (!wxFileExists(GetGprofPath()) || !wxFileExists(GetDotPath()))
        return MessageBox(_T("Failed to locate required tools (gprof, dot). Please check the plugin settings."), wxICON_ERROR);

    Workspace   *ws = m_mgr->GetWorkspace();
    if (!ws)		return MessageBox(_("Unable to get opened workspace."), wxICON_ERROR);

    wxFileName  ws_cfn = ws->GetWorkspaceFileName();

    wxString projectName = ws->GetActiveProjectName();

    BuildMatrixPtr	  mtx = ws->GetBuildMatrix();
    if (!mtx)	   return MessageBox(_("Unable to get current build matrix."), wxICON_ERROR);

    wxString	build_config_name = mtx->GetSelectedConfigurationName();

    BuildConfigPtr	  bldConf = ws->GetProjBuildConf(projectName, build_config_name);
    if (!bldConf)   return MessageBox(_("Unable to get opened workspace."), wxICON_ERROR);

    wxString	projOutputFn = bldConf->GetOutputFileName();
    wxString	projWorkingDir = bldConf->GetWorkingDirectory();

    /*
    myLog("WorkspaceFileName = \"%s\"", ws_cfn.GetFullPath());
    myLog("projectName \"%s\"", projectName);
    myLog("build_config_name = \"%s\"", build_config_name);
    myLog("projOutputFn = \"%s\"", projOutputFn);
    myLog("projWorkingDir = \"%s\"", projWorkingDir);
    */

    wxFileName  cfn(ws_cfn.GetPath(), projOutputFn);
    cfn.Normalize();

    // base path
    const wxString	base_path = ws_cfn.GetPath();

    // check source binary exists
    wxString	bin_fpath = cfn.GetFullPath();
    if (!cfn.Exists()) {
        bin_fpath = wxFileSelector("Please select the binary to analyze", base_path, "", "");
        if (bin_fpath.IsEmpty())		return MessageBox("selected binary was canceled", wxICON_ERROR);

        cfn.Assign(bin_fpath, wxPATH_NATIVE);
    }
    if (!cfn.IsFileExecutable())		return MessageBox("bin/exe isn't executable", wxICON_ERROR);

    // check 'gmon.out' file exists
    wxFileName  gmon_cfn(base_path, GMON_FILENAME_OUT);
    if (!gmon_cfn.Exists())
        gmon_cfn.Normalize();

    wxString	gmonfn = gmon_cfn.GetFullPath();
    if (!gmon_cfn.Exists()) {
        gmonfn = wxFileSelector("Please select the gprof file", gmon_cfn.GetPath(), "gmon", "out");
        if (gmonfn.IsEmpty())		return MessageBox("selected gprof was canceled", wxICON_ERROR);

        gmon_cfn.Assign(gmonfn, wxPATH_NATIVE);
    }

    wxString	bin, arg1, arg2;

    bin = GetGprofPath();
    arg1 = bin_fpath;
    arg2 = gmonfn;

    wxString cmdgprof = wxString::Format("%s %s %s", bin, arg1, arg2);

    // myLog("about to wxExecute(\"%s\")", cmdgprof);

    wxProcess	*proc = new wxProcess(wxPROCESS_REDIRECT);

    // wxStopWatch	sw;

    const int	err = ::wxExecute(cmdgprof, wxEXEC_SYNC, proc);
    // on sync returns 0 (success), -1 (failure / "couldn't be started")

    // myLog("wxExecute() returned err %d, had pid %d", err, (int)proc->GetPid());

    wxInputStream	   *process_is = proc->GetInputStream();
    if (!process_is || !process_is->CanRead())
        return MessageBox(_("wxProcess::GetInputStream() can't be opened, aborting"), wxICON_ERROR);

    // start parsing and writing to dot language file
    GprofParser pgp;

    pgp.GprofParserStream(process_is);

    // myLog("gprof done (read %d lines)", (int) pgp.lines.GetCount());

    delete proc;

    ConfCallGraph conf;

    config_tool->ReadObject(wxT("CallGraph"), &conf);

    DotWriter dotWriter;

    // DotWriter
    dotWriter.SetLineParser(&(pgp.lines));

    int suggestedThreshold = pgp.GetSuggestedNodeThreshold();

    if (suggestedThreshold <= conf.GetTresholdNode()) {
        suggestedThreshold = conf.GetTresholdNode();

        dotWriter.SetDotWriterFromDialogSettings(m_mgr);

    } else {
        dotWriter.SetDotWriterFromDetails(conf.GetColorsNode(),
                                          conf.GetColorsEdge(),
                                          suggestedThreshold,
                                          conf.GetTresholdEdge(),
                                          conf.GetHideParams(),
                                          conf.GetStripParams(),
                                          conf.GetHideNamespaces());

        wxString	suggest_msg = wxString::Format(_("The CallGraph plugin has suggested node threshold %d to speed-up the call graph creation. You can alter it on the call graph panel."), suggestedThreshold);

        MessageBox(suggest_msg, wxICON_INFORMATION);
    }

    dotWriter.WriteToDotLanguage();

    // build output dir
    cfn.Assign(base_path, "");
    cfn.AppendDir(CALLGRAPH_DIR);
    cfn.Normalize();

    if (!cfn.DirExists())	   cfn.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

    cfn.SetFullName(DOT_FILENAME_TXT);
    wxString dot_fn = cfn.GetFullPath();

    dotWriter.SendToDotAppOutputDirectory(dot_fn);

    cfn.SetFullName(DOT_FILENAME_PNG);
    wxString output_png_fn = cfn.GetFullPath();

    // delete any existing PNG
    if (wxFileExists(output_png_fn))	wxRemoveFile(output_png_fn);

    wxString cmddot_ln;

    cmddot_ln << GetDotPath() << " -Tpng -o" << output_png_fn << " " << dot_fn;

    // myLog("wxExecute(\"%s\")", cmddot_ln);

    wxExecute(cmddot_ln, wxEXEC_SYNC);

    // myLog("dot done");

    if (!wxFileExists(output_png_fn))
        return MessageBox(_("Failed to open file CallGraph.png. Please check the project settings, rebuild the project and try again."), wxICON_INFORMATION);

    // show image and create table in the editor tab page
    uicallgraphpanel	*panel = new uicallgraphpanel(m_mgr->GetEditorPaneNotebook(), m_mgr, output_png_fn, base_path, suggestedThreshold, &(pgp.lines));

    wxString	tstamp = wxDateTime::Now().Format(wxT(" %Y-%m-%d %H:%M:%S"));

    wxString	  title = wxT("Call graph for \"") + output_png_fn + wxT("\" " + tstamp);

    m_mgr->AddEditorPage(panel, title);
}

//---- Show Settings Dialog ---------------------------------------------------

void CallGraph::OnSettings(wxCommandEvent& event)
{
    // open the settings dialog
    wxWindow	*win = m_mgr->GetTheApp()->GetTopWindow();

    uisettingsdlg	   uisdlg(win, m_mgr);

    uisdlg.ShowModal();
}

//---- Message Box (combo) ----------------------------------------------------

void CallGraph::MessageBox(const wxString &msg, unsigned long icon_mask)
{
    wxWindow *win = m_mgr->GetTheApp()->GetTopWindow();

    ::wxMessageBox(msg, wxT("CallGraph"), wxOK | icon_mask, win);
}
