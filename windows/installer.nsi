; =====================================================================
;  AutoQ (created by Qisit) — установщик для Windows (NSIS 3, Unicode)
;  Сборка: makensis installer.nsi  (рядом должны лежать AutoQ.exe,
;  app.ico и README_WINDOWS.txt)
; =====================================================================
Unicode true
ManifestDPIAware true

!define APP_NAME    "AutoQ"
!define APP_EXE     "AutoQ.exe"
!define APP_VERSION "1.18.0 beta"
!define APP_AUTHOR  "Qisit"
!define APP_REGKEY  "Software\Microsoft\Windows\CurrentVersion\Uninstall\AutoQ"

; Подпись (необязательно): makensis -DSIGN_CMD=/путь/windows/sign.sh installer.nsi
; Подписывается и деинсталлятор (он создаётся внутри сборки), и сам установщик.
!ifdef SIGN_CMD
    !uninstfinalize '"${SIGN_CMD}" "%1"' = 0
    !finalize '"${SIGN_CMD}" "%1"' = 0
!endif

Name "${APP_NAME}"
OutFile "AutoQ-Setup.exe"
InstallDir "$PROGRAMFILES64\AutoQ"
InstallDirRegKey HKLM "${APP_REGKEY}" "InstallLocation"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
BrandingText "${APP_NAME} ${APP_VERSION} · created by ${APP_AUTHOR}"

VIProductVersion "1.18.0.0"
VIAddVersionKey "ProductName" "${APP_NAME}"
VIAddVersionKey "CompanyName" "${APP_AUTHOR}"
VIAddVersionKey "FileDescription" "${APP_NAME} Setup"
VIAddVersionKey "FileVersion" "${APP_VERSION}"
VIAddVersionKey "ProductVersion" "${APP_VERSION}"
VIAddVersionKey "LegalCopyright" "created by ${APP_AUTHOR}"

!include "MUI2.nsh"
!include "x64.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"

; AutoQ ставит обновление сам: AutoQ-Setup.exe /S /UPDATE (updater.cpp) — тихо,
; без возврата удалённого ярлыка на рабочем столе, после установки запускает AutoQ
Var IsUpdate

!define MUI_ICON   "app.ico"
!define MUI_UNICON "app.ico"
!define MUI_ABORTWARNING

!define MUI_WELCOMEPAGE_TITLE "$(TxtWelcomeTitle)"
!define MUI_WELCOMEPAGE_TEXT "$(TxtWelcome)"
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APP_EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "$(TxtRun)"
!define MUI_FINISHPAGE_SHOWREADME "$INSTDIR\$(TxtReadmeFile)"
!define MUI_FINISHPAGE_SHOWREADME_TEXT "$(TxtOpenGuide)"
!define MUI_FINISHPAGE_SHOWREADME_NOTCHECKED

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; язык установщика: по языку Windows, с выбором в начале (русский / English)
!define MUI_LANGDLL_REGISTRY_ROOT "HKCU"
!define MUI_LANGDLL_REGISTRY_KEY "Software\AutoQ"
!define MUI_LANGDLL_REGISTRY_VALUENAME "InstallerLanguage"
!insertmacro MUI_LANGUAGE "Russian"
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_RESERVEFILE_LANGDLL

LangString TxtWelcomeTitle ${LANG_RUSSIAN} "Установка ${APP_NAME}"
LangString TxtWelcomeTitle ${LANG_ENGLISH} "${APP_NAME} Setup"
LangString TxtWelcome ${LANG_RUSSIAN} "Мастер установит ${APP_NAME} ${APP_VERSION} (created by ${APP_AUTHOR}).$\r$\n$\r$\nПрограмме нужны права администратора: игры вроде Genshin Impact запущены от администратора, и Windows не пропускает в них клики от обычных программ.$\r$\n$\r$\nНажмите «Далее», чтобы продолжить."
LangString TxtWelcome ${LANG_ENGLISH} "This wizard will install ${APP_NAME} ${APP_VERSION} (created by ${APP_AUTHOR}).$\r$\n$\r$\nThe app needs administrator rights: games like Genshin Impact run as administrator, and Windows doesn’t let regular programs click into them.$\r$\n$\r$\nClick Next to continue."
LangString TxtRun ${LANG_RUSSIAN} "Запустить ${APP_NAME}"
LangString TxtRun ${LANG_ENGLISH} "Run ${APP_NAME}"
LangString TxtOpenGuide ${LANG_RUSSIAN} "Открыть инструкцию"
LangString TxtOpenGuide ${LANG_ENGLISH} "Open the guide"
LangString TxtReadmeFile ${LANG_RUSSIAN} "README_WINDOWS.txt"
LangString TxtReadmeFile ${LANG_ENGLISH} "README_WINDOWS_EN.txt"
LangString TxtGuide ${LANG_RUSSIAN} "Инструкция"
LangString TxtGuide ${LANG_ENGLISH} "Guide"
LangString TxtUninstall ${LANG_RUSSIAN} "Удалить ${APP_NAME}"
LangString TxtUninstall ${LANG_ENGLISH} "Uninstall ${APP_NAME}"
LangString TxtOnly64 ${LANG_RUSSIAN} "${APP_NAME} работает только на 64-битной Windows 10/11."
LangString TxtOnly64 ${LANG_ENGLISH} "${APP_NAME} runs only on 64-bit Windows 10/11."
LangString TxtSecMain ${LANG_RUSSIAN} "Программа (обязательно)"
LangString TxtSecMain ${LANG_ENGLISH} "Program (required)"
LangString TxtSecDesktop ${LANG_RUSSIAN} "Ярлык на рабочем столе"
LangString TxtSecDesktop ${LANG_ENGLISH} "Desktop shortcut"
LangString DescMain ${LANG_RUSSIAN} "Сама программа, ярлыки в меню «Пуск» и деинсталлятор."
LangString DescMain ${LANG_ENGLISH} "The program, Start menu shortcuts and the uninstaller."
LangString DescDesktop ${LANG_RUSSIAN} "Значок программы на рабочем столе."
LangString DescDesktop ${LANG_ENGLISH} "A program icon on the desktop."
LangString TxtDeleteSettings ${LANG_RUSSIAN} "Удалить также настройки и профили?"
LangString TxtDeleteSettings ${LANG_ENGLISH} "Also delete settings and profiles?"

Function .onInit
    ${GetParameters} $R0
    ClearErrors
    ${GetOptions} $R0 "/UPDATE" $R1
    ${IfNot} ${Errors}
        StrCpy $IsUpdate 1
    ${EndIf}
    !insertmacro MUI_LANGDLL_DISPLAY
    ${IfNot} ${RunningX64}
        MessageBox MB_ICONSTOP "$(TxtOnly64)" /SD IDOK
        Abort
    ${EndIf}
    SetRegView 64
FunctionEnd

Function un.onInit
    !insertmacro MUI_UNGETLANGUAGE
    SetRegView 64
FunctionEnd

; Закрыть запущенную копию, чтобы exe можно было перезаписать
!macro CloseRunningApp
    nsExec::Exec 'taskkill /F /IM "${APP_EXE}"'
    Pop $0
    Sleep 500
!macroend


Section "$(TxtSecMain)" SecMain
    SectionIn RO
    !insertmacro CloseRunningApp
    SetOutPath "$INSTDIR"
    File "${APP_EXE}"
    File "README_WINDOWS.txt"
    File "README_WINDOWS_EN.txt"
    File "app.ico"
    WriteUninstaller "$INSTDIR\Uninstall.exe"

    ; язык программы — как выбрали в установщике (если в AutoQ его ещё не меняли)
    ReadRegStr $2 HKCU "Software\AutoQ\config" "language"
    ${If} $2 == ""
        ${If} $LANGUAGE == ${LANG_ENGLISH}
            WriteRegStr HKCU "Software\AutoQ\config" "language" "en"
        ${Else}
            WriteRegStr HKCU "Software\AutoQ\config" "language" "ru"
        ${EndIf}
    ${EndIf}

    CreateDirectory "$SMPROGRAMS\${APP_NAME}"
    CreateShortcut "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}" "" "$INSTDIR\app.ico"
    CreateShortcut "$SMPROGRAMS\${APP_NAME}\$(TxtGuide).lnk" "$INSTDIR\$(TxtReadmeFile)"
    CreateShortcut "$SMPROGRAMS\${APP_NAME}\$(TxtUninstall).lnk" "$INSTDIR\Uninstall.exe"

    ; запись для «Параметры → Приложения»
    WriteRegStr   HKLM "${APP_REGKEY}" "DisplayName"     "${APP_NAME}"
    WriteRegStr   HKLM "${APP_REGKEY}" "DisplayVersion"  "${APP_VERSION}"
    WriteRegStr   HKLM "${APP_REGKEY}" "Publisher"       "${APP_AUTHOR}"
    WriteRegStr   HKLM "${APP_REGKEY}" "DisplayIcon"     "$INSTDIR\app.ico"
    WriteRegStr   HKLM "${APP_REGKEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr   HKLM "${APP_REGKEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegDWORD HKLM "${APP_REGKEY}" "NoModify" 1
    WriteRegDWORD HKLM "${APP_REGKEY}" "NoRepair" 1
    WriteRegDWORD HKLM "${APP_REGKEY}" "EstimatedSize" 30000
SectionEnd

Section "$(TxtSecDesktop)" SecDesktop
    ; обновление из AutoQ: ярлык, который удалили, не возвращаем
    ${If} $IsUpdate == 1
    ${AndIfNot} ${FileExists} "$DESKTOP\${APP_NAME}.lnk"
        Return
    ${EndIf}
    CreateShortcut "$DESKTOP\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}" "" "$INSTDIR\app.ico"
SectionEnd

; обновление из AutoQ: запустить новую версию (с --restarted она дождётся, пока старая закроется)
Function .onInstSuccess
    ${If} $IsUpdate == 1
        Exec '"$INSTDIR\${APP_EXE}" --restarted'
    ${EndIf}
FunctionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecMain}    "$(DescMain)"
    !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "$(DescDesktop)"
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
    !insertmacro CloseRunningApp
    ; убрать автозапуск, если его включали в программе (ключ Run + задача планировщика)
    nsExec::Exec 'schtasks /Delete /TN "AutoQ" /F'
    Pop $0
    DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "AutoQ"
    DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run" "AutoQ"

    Delete "$INSTDIR\${APP_EXE}"
    Delete "$INSTDIR\README_WINDOWS.txt"
    Delete "$INSTDIR\README_WINDOWS_EN.txt"
    Delete "$INSTDIR\app.ico"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir /r "$INSTDIR\AutoQ-update"
    RMDir  "$INSTDIR"

    Delete "$DESKTOP\${APP_NAME}.lnk"
    Delete "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk"
    Delete "$SMPROGRAMS\${APP_NAME}\Инструкция.lnk"
    Delete "$SMPROGRAMS\${APP_NAME}\Guide.lnk"
    Delete "$SMPROGRAMS\${APP_NAME}\Удалить ${APP_NAME}.lnk"
    Delete "$SMPROGRAMS\${APP_NAME}\Uninstall ${APP_NAME}.lnk"
    RMDir  "$SMPROGRAMS\${APP_NAME}"

    DeleteRegKey HKLM "${APP_REGKEY}"

    MessageBox MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2 "$(TxtDeleteSettings)" /SD IDNO IDNO keep_settings
        DeleteRegKey HKCU "Software\AutoQ"
    keep_settings:
SectionEnd
