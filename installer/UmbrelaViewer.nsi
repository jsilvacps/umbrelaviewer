; ─────────────────────────────────────────────────────────────────────────────
;  Umbrela Viewer — Installer Script (NSIS 3.x)
;  Instala em %LOCALAPPDATA%\UmbrelaViewer — NÃO requer privilégio de admin.
; ─────────────────────────────────────────────────────────────────────────────

Unicode True

!define APP_NAME      "Umbrela Viewer"
!define APP_VERSION   "1.0"
!define APP_EXE       "UmbrelaViewer.exe"
!define APP_PUBLISHER "Jean Silva"
!define APP_URL       "https://github.com/jsilvacps/umbrelaviewer"
!define UNREG_KEY     "Software\Microsoft\Windows\CurrentVersion\Uninstall\UmbrelaViewer"
!define RUN_KEY       "Software\Microsoft\Windows\CurrentVersion\Run"

; ─── Output ──────────────────────────────────────────────────────────────────
Name          "${APP_NAME}"
OutFile       "..\dist\UmbrelaViewer_Setup.exe"
InstallDir    "$LOCALAPPDATA\UmbrelaViewer"
InstallDirRegKey HKCU "Software\UmbrelaViewer" "InstallPath"

; Sem UAC: instala no perfil do usuário atual
RequestExecutionLevel user

; ─── Interface MUI2 ──────────────────────────────────────────────────────────
!include "MUI2.nsh"
!include "WinMessages.nsh"

!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE    "Bem-vindo ao Umbrela Viewer ${APP_VERSION}"
!define MUI_WELCOMEPAGE_TEXT     "Este assistente instalará o Umbrela Viewer no seu computador.$\r$\n$\r$\nO programa será instalado na sua conta de usuário — nenhuma senha de administrador é necessária.$\r$\n$\r$\nClique em Avançar para continuar."
!define MUI_FINISHPAGE_RUN       "$INSTDIR\${APP_EXE}"
!define MUI_FINISHPAGE_RUN_TEXT  "Iniciar Umbrela Viewer agora"

; Ícone do instalador (usa o próprio EXE como ícone — NSIS extrai automaticamente)
; !define MUI_ICON "..\dist\${APP_EXE}"   ; descomente se quiser usar o ícone do EXE

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "PortugueseBR"

; ─────────────────────────────────────────────────────────────────────────────
;  Seção principal
; ─────────────────────────────────────────────────────────────────────────────
Section "Umbrela Viewer" SecMain
    SectionIn RO   ; obrigatório

    SetOutPath "$INSTDIR"
    File "..\dist\${APP_EXE}"

    ; ── Atalhos ──────────────────────────────────────────────────────────────
    CreateShortcut "$DESKTOP\Umbrela Viewer.lnk"         "$INSTDIR\${APP_EXE}"
    CreateDirectory "$SMPROGRAMS\Umbrela Viewer"
    CreateShortcut "$SMPROGRAMS\Umbrela Viewer\Umbrela Viewer.lnk"  "$INSTDIR\${APP_EXE}"
    CreateShortcut "$SMPROGRAMS\Umbrela Viewer\Desinstalar.lnk"     "$INSTDIR\Desinstalar.exe"

    ; ── Iniciar com o Windows (HKCU — sem admin) ─────────────────────────────
    WriteRegStr HKCU "${RUN_KEY}" "UmbrelaViewer" '"$INSTDIR\${APP_EXE}"'

    ; ── Salva caminho de instalação ───────────────────────────────────────────
    WriteRegStr HKCU "Software\UmbrelaViewer" "InstallPath" "$INSTDIR"

    ; ── Adiciona a "Programas e Recursos" ────────────────────────────────────
    WriteRegStr   HKCU "${UNREG_KEY}" "DisplayName"     "${APP_NAME}"
    WriteRegStr   HKCU "${UNREG_KEY}" "DisplayVersion"  "${APP_VERSION}"
    WriteRegStr   HKCU "${UNREG_KEY}" "Publisher"       "${APP_PUBLISHER}"
    WriteRegStr   HKCU "${UNREG_KEY}" "URLInfoAbout"    "${APP_URL}"
    WriteRegStr   HKCU "${UNREG_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr   HKCU "${UNREG_KEY}" "UninstallString" '"$INSTDIR\Desinstalar.exe"'
    WriteRegDWORD HKCU "${UNREG_KEY}" "NoModify"        1
    WriteRegDWORD HKCU "${UNREG_KEY}" "NoRepair"        1

    ; ── Grava o desinstalador ─────────────────────────────────────────────────
    WriteUninstaller "$INSTDIR\Desinstalar.exe"
SectionEnd

; ─────────────────────────────────────────────────────────────────────────────
;  Desinstalador
; ─────────────────────────────────────────────────────────────────────────────
Section "Uninstall"
    ; Encerra processo se estiver rodando
    ExecWait 'taskkill /F /IM "${APP_EXE}"' $0

    ; Remove arquivos
    Delete "$INSTDIR\${APP_EXE}"
    Delete "$INSTDIR\Desinstalar.exe"
    RMDir  "$INSTDIR"

    ; Remove atalhos
    Delete "$DESKTOP\Umbrela Viewer.lnk"
    Delete "$SMPROGRAMS\Umbrela Viewer\Umbrela Viewer.lnk"
    Delete "$SMPROGRAMS\Umbrela Viewer\Desinstalar.lnk"
    RMDir  "$SMPROGRAMS\Umbrela Viewer"

    ; Remove registro
    DeleteRegValue HKCU "${RUN_KEY}"    "UmbrelaViewer"
    DeleteRegKey   HKCU "${UNREG_KEY}"
    DeleteRegKey   HKCU "Software\UmbrelaViewer"

    MessageBox MB_OK "Umbrela Viewer foi removido com sucesso."
SectionEnd
