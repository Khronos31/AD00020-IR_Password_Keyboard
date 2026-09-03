use std::fs;
use std::io::{self, BufRead, Write};
use std::path::{Path, PathBuf};

use crossterm::cursor::{Hide, Show};
use crossterm::event::{self, Event, KeyCode, KeyEvent, KeyModifiers};
use crossterm::execute;
use crossterm::terminal::{
    disable_raw_mode, enable_raw_mode, Clear, ClearType, EnterAlternateScreen, LeaveAlternateScreen,
};
use zeroize::{Zeroize, Zeroizing};

const APP_TITLE: &str = "AD00020 IR Password Keyboard Configuration Tool";
const SLOT_CAPACITY: usize = 16;
const MAX_PASSWORD_BYTES: usize = 63;
const DEFAULT_OUTPUT_PATH: &str = "build/ad00020-tui-config.placeholder.json";
const CONTROL_NAMES: [ControlName; 3] =
    [ControlName::Unlock, ControlName::Lock, ControlName::Confirm];

#[derive(Clone, Copy, PartialEq, Eq)]
enum ControlName {
    Unlock,
    Lock,
    Confirm,
}

impl ControlName {
    fn as_str(self) -> &'static str {
        match self {
            Self::Unlock => "Unlock",
            Self::Lock => "Lock",
            Self::Confirm => "Confirm",
        }
    }
}

struct PasswordEntry {
    keycode: String,
    alias: String,
    password: Zeroizing<String>,
}

impl PasswordEntry {
    fn new(keycode: String, alias: String, password: Zeroizing<String>) -> Self {
        Self {
            keycode,
            alias,
            password,
        }
    }

    fn keycode(&self) -> &str {
        &self.keycode
    }

    fn alias(&self) -> &str {
        &self.alias
    }

    // The future USB backend can borrow these bytes directly. This method is
    // deliberately not used by the placeholder JSON backend or the renderer.
    #[allow(dead_code)]
    fn secret_bytes(&self) -> &[u8] {
        self.password.as_bytes()
    }
}

struct Configuration {
    controls: [String; 3],
    passwords: Vec<PasswordEntry>,
}

impl Configuration {
    fn new() -> Self {
        Self {
            controls: std::array::from_fn(|_| String::new()),
            passwords: Vec::new(),
        }
    }

    fn control_code(&self, control: ControlName) -> &str {
        &self.controls[control as usize]
    }

    fn set_control_code(&mut self, control: ControlName, code: String) {
        self.controls[control as usize] = code;
    }

    fn add_password(&mut self, entry: PasswordEntry) -> Result<(), ConfigError> {
        if self.passwords.len() >= SLOT_CAPACITY {
            return Err(ConfigError::SlotCapacity);
        }
        if entry.keycode().is_empty() {
            return Err(ConfigError::EmptyKeycode);
        }
        if self
            .passwords
            .iter()
            .any(|item| item.keycode() == entry.keycode())
        {
            return Err(ConfigError::DuplicateKeycode);
        }
        validate_password(&entry.password)?;
        self.passwords.push(entry);
        Ok(())
    }

    fn remove_password(&mut self, index: usize) -> Result<PasswordEntry, ConfigError> {
        if index >= self.passwords.len() {
            return Err(ConfigError::InvalidSlot);
        }
        Ok(self.passwords.remove(index))
    }

    fn password_count(&self) -> usize {
        self.passwords.len()
    }
}

impl Drop for Configuration {
    fn drop(&mut self) {
        for entry in &mut self.passwords {
            entry.password.zeroize();
        }
    }
}

struct ConfigurationModel {
    staged: Configuration,
    dirty: bool,
}

impl ConfigurationModel {
    fn new() -> Self {
        Self {
            staged: Configuration::new(),
            dirty: false,
        }
    }

    fn stage_control_code(&mut self, control: ControlName, code: String) {
        self.staged.set_control_code(control, code);
        self.dirty = true;
    }

    fn password_count(&self) -> usize {
        self.staged.password_count()
    }

    fn stage_password(
        &mut self,
        keycode: String,
        alias: String,
        password: Zeroizing<String>,
    ) -> Result<(), ConfigError> {
        self.staged
            .add_password(PasswordEntry::new(keycode, alias, password))?;
        self.dirty = true;
        Ok(())
    }

    fn remove_password(&mut self, index: usize) -> Result<PasswordEntry, ConfigError> {
        let removed = self.staged.remove_password(index)?;
        self.dirty = true;
        Ok(removed)
    }

    fn discard_unsaved(&mut self) {
        // Replacing the model clears all staged secrets through Drop. There is
        // no clone-based rollback path for password-bearing state.
        self.staged = Configuration::new();
        self.dirty = false;
    }

    fn mark_applied(&mut self) {
        self.dirty = false;
    }
}

#[derive(Debug, PartialEq, Eq)]
enum ConfigError {
    SlotCapacity,
    EmptyKeycode,
    DuplicateKeycode,
    EmptyPassword,
    PasswordTooLong,
    PasswordNonAscii,
    PasswordNonPrintable,
    InvalidSlot,
}

impl std::fmt::Display for ConfigError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let message = match self {
            Self::SlotCapacity => "the maximum of 16 password slots is already used",
            Self::EmptyKeycode => "the scanned keycode cannot be empty",
            Self::DuplicateKeycode => "that keycode is already assigned",
            Self::EmptyPassword => "the password cannot be empty",
            Self::PasswordTooLong => "the password is longer than 63 bytes",
            Self::PasswordNonAscii => "the password must contain ASCII characters only",
            Self::PasswordNonPrintable => "the password must contain printable ASCII only",
            Self::InvalidSlot => "the selected slot does not exist",
        };
        formatter.write_str(message)
    }
}

impl std::error::Error for ConfigError {}

#[derive(Debug)]
enum ToolError {
    Io(io::Error),
    Config(ConfigError),
    Apply(String),
}

impl std::fmt::Display for ToolError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Io(error) => write!(formatter, "I/O error: {error}"),
            Self::Config(error) => error.fmt(formatter),
            Self::Apply(error) => formatter.write_str(error),
        }
    }
}

impl std::error::Error for ToolError {}

impl From<io::Error> for ToolError {
    fn from(error: io::Error) -> Self {
        Self::Io(error)
    }
}

impl From<ConfigError> for ToolError {
    fn from(error: ConfigError) -> Self {
        Self::Config(error)
    }
}

fn validate_password(password: &str) -> Result<(), ConfigError> {
    if password.is_empty() {
        return Err(ConfigError::EmptyPassword);
    }
    if !password.is_ascii() {
        return Err(ConfigError::PasswordNonAscii);
    }
    if password.len() > MAX_PASSWORD_BYTES {
        return Err(ConfigError::PasswordTooLong);
    }
    if password
        .as_bytes()
        .iter()
        .any(|byte| !(0x20..=0x7e).contains(byte))
    {
        return Err(ConfigError::PasswordNonPrintable);
    }
    Ok(())
}

trait ConfigurationBackend {
    fn apply(&self, configuration: &Configuration) -> Result<(), ToolError>;
}

struct PlaceholderBackend {
    output_path: PathBuf,
}

impl PlaceholderBackend {
    fn new(output_path: PathBuf) -> Self {
        Self { output_path }
    }
}

impl ConfigurationBackend for PlaceholderBackend {
    fn apply(&self, configuration: &Configuration) -> Result<(), ToolError> {
        apply_config(configuration, &self.output_path)
    }
}

fn apply_config(configuration: &Configuration, output_path: &Path) -> Result<(), ToolError> {
    let controls = serde_json::json!({
        "Unlock": configuration.control_code(ControlName::Unlock),
        "Lock": configuration.control_code(ControlName::Lock),
        "Confirm": configuration.control_code(ControlName::Confirm),
    });
    let slots: Vec<serde_json::Value> = configuration
        .passwords
        .iter()
        .map(|entry| {
            serde_json::json!({
                "keycode": entry.keycode(),
                "alias": entry.alias(),
                "configured": true,
            })
        })
        .collect();
    let document = serde_json::json!({
        "format": "AD00020 IR Password Keyboard configuration placeholder",
        "version": "0.3.0",
        "controls": controls,
        "slots": slots,
        "slot_count": configuration.password_count(),
        "secret_values_written": false,
        "warning": "Placeholder metadata only; use a real device backend before deployment.",
    });
    let serialized = serde_json::to_vec_pretty(&document)
        .map_err(|error| ToolError::Apply(format!("could not serialize configuration: {error}")))?;
    if let Some(parent) = output_path.parent() {
        fs::create_dir_all(parent)?;
    }
    fs::write(output_path, serialized)?;
    Ok(())
}

// Replaceable transport seam. It intentionally reads one standard-input line
// and only removes that line's terminator; it does not parse or validate NEC.
fn scan_code<R: BufRead>(reader: &mut R) -> Result<String, ToolError> {
    let mut value = String::new();
    let bytes = reader.read_line(&mut value)?;
    if bytes == 0 {
        return Err(ToolError::Apply(
            "no scan code received (EOF cancels this operation)".to_string(),
        ));
    }
    if value.ends_with('\n') {
        value.pop();
        if value.ends_with('\r') {
            value.pop();
        }
    } else if value.ends_with('\r') {
        value.pop();
    }
    Ok(value)
}

fn pending_control_from_scan(control: ControlName, code: String) -> Option<(ControlName, String)> {
    if code.is_empty() {
        None
    } else {
        Some((control, code))
    }
}

fn selectable_line(selected: bool, label: &str) -> String {
    format!("{}{}", if selected { "> " } else { "  " }, label)
}

// Raw mode disables the Unix terminal's LF-to-CRLF output translation. Always
// return to column zero explicitly so menu rows do not drift into a staircase.
fn write_terminal_line<W: Write>(writer: &mut W, text: &str) -> io::Result<()> {
    writer.write_all(text.as_bytes())?;
    writer.write_all(b"\r\n")
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum Screen {
    Main,
    Controls,
    ControlConfirmation,
    Remove,
    RemoveConfirmation,
    SaveConfirmation,
    ExitConfirmation,
    Notice,
}

struct TerminalGuard {
    active: bool,
    suspended: bool,
}

impl TerminalGuard {
    fn enter() -> Result<Self, ToolError> {
        enable_raw_mode()?;
        let mut stdout = io::stdout();
        if let Err(error) = execute!(stdout, EnterAlternateScreen, Hide) {
            let _ = disable_raw_mode();
            return Err(ToolError::Io(error));
        }
        Ok(Self {
            active: true,
            suspended: false,
        })
    }

    fn suspend(&mut self) -> Result<(), ToolError> {
        if !self.active || self.suspended {
            return Ok(());
        }
        disable_raw_mode()?;
        execute!(io::stdout(), Show, LeaveAlternateScreen)?;
        self.suspended = true;
        Ok(())
    }

    fn resume(&mut self) -> Result<(), ToolError> {
        if !self.active || !self.suspended {
            return Ok(());
        }
        // From this point onward, Drop must attempt normal cleanup even if
        // re-entering the alternate screen fails partway through.
        self.suspended = false;
        execute!(io::stdout(), EnterAlternateScreen, Hide)?;
        if let Err(error) = enable_raw_mode() {
            let _ = execute!(io::stdout(), Show, LeaveAlternateScreen);
            self.active = false;
            return Err(ToolError::Io(error));
        }
        Ok(())
    }
}

impl Drop for TerminalGuard {
    fn drop(&mut self) {
        if self.active && !self.suspended {
            let _ = disable_raw_mode();
            let _ = execute!(io::stdout(), Show, LeaveAlternateScreen);
        }
    }
}

struct Tui {
    terminal: TerminalGuard,
    model: ConfigurationModel,
    backend: PlaceholderBackend,
    screen: Screen,
    selected: usize,
    status: String,
    pending_control: Option<(ControlName, String)>,
    pending_remove: Option<usize>,
}

impl Tui {
    fn new(output_path: PathBuf) -> Result<Self, ToolError> {
        Ok(Self {
            terminal: TerminalGuard::enter()?,
            model: ConfigurationModel::new(),
            backend: PlaceholderBackend::new(output_path),
            screen: Screen::Main,
            selected: 0,
            status: String::new(),
            pending_control: None,
            pending_remove: None,
        })
    }

    fn run(mut self) -> Result<(), ToolError> {
        self.draw()?;
        loop {
            let event = event::read()?;
            let (continue_running, redraw) = match event {
                Event::Key(key) => self.handle_key(key)?,
                Event::Resize(_, _) => (true, true),
                _ => (true, false),
            };
            if redraw {
                self.draw()?;
            }
            if !continue_running {
                return Ok(());
            }
        }
    }

    fn handle_key(&mut self, key: KeyEvent) -> Result<(bool, bool), ToolError> {
        if is_cancel_key(key) {
            self.model.discard_unsaved();
            return Ok((false, false));
        }
        if key.modifiers.contains(KeyModifiers::CONTROL) && key.code == KeyCode::Char('d') {
            return Ok((true, false));
        }
        match key.code {
            KeyCode::Up => {
                self.move_selection(-1);
                Ok((true, true))
            }
            KeyCode::Down => {
                self.move_selection(1);
                Ok((true, true))
            }
            KeyCode::Enter => Ok((!self.activate()?, true)),
            _ => Ok((true, false)),
        }
    }

    fn move_selection(&mut self, delta: isize) {
        let count = self.item_count();
        if count == 0 {
            self.selected = 0;
            return;
        }
        self.selected = if delta < 0 {
            (self.selected + count - 1) % count
        } else {
            (self.selected + 1) % count
        };
    }

    fn item_count(&self) -> usize {
        match self.screen {
            Screen::Main => 5,
            Screen::Controls => 4,
            Screen::ControlConfirmation => 2,
            Screen::Remove => self.model.password_count() + 1,
            Screen::RemoveConfirmation => 2,
            Screen::SaveConfirmation => 2,
            Screen::ExitConfirmation => 2,
            Screen::Notice => 1,
        }
    }

    fn activate(&mut self) -> Result<bool, ToolError> {
        match self.screen {
            Screen::Main => match self.selected {
                0 => self.show(Screen::Controls, ""),
                1 => self.add_password_flow()?,
                2 => self.show(Screen::Remove, ""),
                3 => self.show(Screen::SaveConfirmation, ""),
                4 => {
                    if self.model.dirty {
                        self.show(
                            Screen::ExitConfirmation,
                            "Unsaved changes will be discarded.",
                        );
                    } else {
                        return Ok(true);
                    }
                }
                _ => unreachable!(),
            },
            Screen::Controls => {
                if self.selected == 3 {
                    self.show(Screen::Main, "");
                } else {
                    self.control_scan_flow(CONTROL_NAMES[self.selected])?;
                }
            }
            Screen::ControlConfirmation => {
                if self.selected == 0 {
                    if let Some((control, code)) = self.pending_control.take() {
                        self.model.stage_control_code(control, code);
                        self.show(Screen::Controls, "Control keycode staged.");
                    }
                } else {
                    self.pending_control = None;
                    self.show(Screen::Controls, "Control keycode scan cancelled.");
                }
            }
            Screen::Remove => {
                if self.selected == self.model.password_count() {
                    self.show(Screen::Main, "");
                } else {
                    self.pending_remove = Some(self.selected);
                    self.show(Screen::RemoveConfirmation, "");
                }
            }
            Screen::RemoveConfirmation => {
                if self.selected == 1 {
                    if let Some(index) = self.pending_remove.take() {
                        let _removed = self.model.remove_password(index)?;
                        self.show(
                            Screen::Remove,
                            "Password slot removed from the staged configuration.",
                        );
                    }
                } else {
                    self.pending_remove = None;
                    self.show(Screen::Remove, "Removal cancelled.");
                }
            }
            Screen::SaveConfirmation => {
                if self.selected == 1 {
                    self.backend.apply(&self.model.staged)?;
                    self.model.mark_applied();
                    self.show(
                        Screen::Notice,
                        "Configuration saved (secrets were not written).",
                    );
                } else {
                    self.show(
                        Screen::Main,
                        "Save cancelled; changes remain staged in memory.",
                    );
                }
            }
            Screen::ExitConfirmation => {
                if self.selected == 1 {
                    self.model.discard_unsaved();
                    return Ok(true);
                }
                self.show(Screen::Main, "");
            }
            Screen::Notice => self.show(Screen::Main, ""),
        }
        Ok(false)
    }

    fn show(&mut self, screen: Screen, status: &str) {
        self.screen = screen;
        self.selected = 0;
        self.status = status.to_string();
    }

    fn read_line_with_scan(&mut self, prompt: &str) -> Result<String, ToolError> {
        self.terminal.suspend()?;
        let result = {
            let mut stdout = io::stdout();
            write!(stdout, "{prompt}")?;
            stdout.flush()?;
            let stdin = io::stdin();
            let mut reader = stdin.lock();
            scan_code(&mut reader)
        };
        let resume_result = self.terminal.resume();
        resume_result.and(result)
    }

    fn read_secret(&mut self, prompt: &str) -> Result<Zeroizing<String>, ToolError> {
        self.terminal.suspend()?;
        let result = rpassword::prompt_password(prompt)
            .map(Zeroizing::new)
            .map_err(ToolError::Io);
        let resume_result = self.terminal.resume();
        resume_result.and(result)
    }

    fn control_scan_flow(&mut self, control: ControlName) -> Result<(), ToolError> {
        let code = self.read_line_with_scan(&format!(
            "Press the button for {} (placeholder; enter a line): ",
            control.as_str()
        ))?;
        let Some(pending) = pending_control_from_scan(control, code) else {
            self.show(
                Screen::Controls,
                "Control keycode scan cancelled; no code entered.",
            );
            return Ok(());
        };
        self.pending_control = Some(pending);
        self.show(
            Screen::ControlConfirmation,
            &format!("Scanned {}; choose Apply or Cancel.", control.as_str()),
        );
        Ok(())
    }

    fn add_password_flow(&mut self) -> Result<(), ToolError> {
        if self.model.password_count() >= SLOT_CAPACITY {
            self.show(Screen::Main, "All 16 password slots are already used.");
            return Ok(());
        }
        let keycode = self.read_line_with_scan(
            "Press the button on the remote control to scan KEYCODE (placeholder; enter a line): ",
        )?;
        let alias = self.read_line_with_scan("Alias (optional): ")?;
        let password = self.read_secret("Type Password: ")?;
        let confirmation = self.read_secret("Retype Password: ")?;
        if password.as_str() != confirmation.as_str() {
            self.show(Screen::Main, "Passwords do not match; nothing was staged.");
            return Ok(());
        }
        match self.model.stage_password(keycode, alias, password) {
            Ok(()) => self.show(
                Screen::Notice,
                "Password added to the staged configuration.",
            ),
            Err(error) => self.show(
                Screen::Main,
                &format!("Password not added: {error}. No password value or length was displayed."),
            ),
        }
        Ok(())
    }

    fn draw(&self) -> Result<(), ToolError> {
        let mut stdout = io::stdout();
        execute!(
            stdout,
            Clear(ClearType::All),
            crossterm::cursor::MoveTo(0, 0)
        )?;
        write_terminal_line(&mut stdout, APP_TITLE)?;
        write_terminal_line(&mut stdout, "")?;
        write_terminal_line(&mut stdout, self.screen_title())?;
        write_terminal_line(&mut stdout, "")?;
        if !self.status.is_empty() {
            write_terminal_line(&mut stdout, &self.safe_public_text(&self.status))?;
            write_terminal_line(&mut stdout, "")?;
        }
        for (index, label) in self.labels().iter().enumerate() {
            write_terminal_line(&mut stdout, &selectable_line(index == self.selected, label))?;
        }
        write_terminal_line(&mut stdout, "")?;
        write_terminal_line(
            &mut stdout,
            "Arrow keys: select    Enter: choose    Ctrl+C: discard and exit",
        )?;
        stdout.flush()?;
        Ok(())
    }

    fn screen_title(&self) -> &'static str {
        match self.screen {
            Screen::Main => "Main menu",
            Screen::Controls => "Configure Control Keycodes",
            Screen::ControlConfirmation => "Control keycode confirmation",
            Screen::Remove => "Remove Password",
            Screen::RemoveConfirmation => "Remove password confirmation",
            Screen::SaveConfirmation => "Apply / Save Configuration",
            Screen::ExitConfirmation => "Exit confirmation",
            Screen::Notice => "Result",
        }
    }

    fn labels(&self) -> Vec<String> {
        match self.screen {
            Screen::Main => vec![
                "Configure Control Keycodes".to_string(),
                format!("Add New Password ({}/16)", self.model.password_count()),
                "Remove Password".to_string(),
                "Apply / Save Configuration".to_string(),
                "Exit".to_string(),
            ],
            Screen::Controls => CONTROL_NAMES
                .iter()
                .map(|control| {
                    let code = self.model.staged.control_code(*control);
                    if code.is_empty() {
                        format!("{} (not configured)", control.as_str())
                    } else {
                        format!(
                            "{} (current: {})",
                            control.as_str(),
                            self.safe_public_text(code)
                        )
                    }
                })
                .chain(std::iter::once("Return to the main menu".to_string()))
                .collect(),
            Screen::ControlConfirmation => vec!["Apply".to_string(), "Cancel".to_string()],
            Screen::Remove => self
                .model
                .staged
                .passwords
                .iter()
                .map(|entry| {
                    let alias = if entry.alias().is_empty() {
                        String::new()
                    } else {
                        format!(" ({})", self.safe_public_text(entry.alias()))
                    };
                    format!("{}{}", self.safe_public_text(entry.keycode()), alias)
                })
                .chain(std::iter::once("Return to the main menu".to_string()))
                .collect(),
            Screen::RemoveConfirmation => {
                let target = self
                    .pending_remove
                    .and_then(|index| self.model.staged.passwords.get(index))
                    .map(|entry| {
                        let alias = if entry.alias().is_empty() {
                            String::new()
                        } else {
                            format!(" ({})", self.safe_public_text(entry.alias()))
                        };
                        format!(
                            "Remove {}{}?",
                            self.safe_public_text(entry.keycode()),
                            alias
                        )
                    })
                    .unwrap_or_else(|| "Remove selected slot?".to_string());
                vec!["Cancel".to_string(), target]
            }
            Screen::SaveConfirmation => vec!["Cancel".to_string(), "Apply / Save".to_string()],
            Screen::ExitConfirmation => {
                vec!["Cancel".to_string(), "Exit without saving".to_string()]
            }
            Screen::Notice => vec!["Press Enter to return to the main menu".to_string()],
        }
    }

    fn safe_public_text(&self, value: &str) -> String {
        value
            .chars()
            .map(|character| {
                if character.is_control() || character == '\u{1b}' {
                    '?'
                } else {
                    character
                }
            })
            .collect()
    }
}

fn is_cancel_key(key: KeyEvent) -> bool {
    key.modifiers.contains(KeyModifiers::CONTROL) && key.code == KeyCode::Char('c')
}

fn run() -> Result<(), ToolError> {
    Tui::new(PathBuf::from(DEFAULT_OUTPUT_PATH))?.run()
}

fn main() {
    if let Err(error) = run() {
        eprintln!("{APP_TITLE}: {error}");
        std::process::exit(1);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::{Cursor, Read};

    fn secret(value: &str) -> Zeroizing<String> {
        Zeroizing::new(value.to_string())
    }

    #[test]
    fn scanner_only_strips_one_line_terminator() {
        let mut reader = Cursor::new("  NEC-raw-01\r\n");
        let value = scan_code(&mut reader).expect("scan should succeed");
        assert_eq!(value, "  NEC-raw-01");

        let mut reader = Cursor::new("literal\\t\n");
        let value = scan_code(&mut reader).expect("scan should succeed");
        assert_eq!(value, "literal\\t");
    }

    #[test]
    fn password_validation_matches_firmware_constraints() {
        assert_eq!(
            validate_password("päss"),
            Err(ConfigError::PasswordNonAscii)
        );
        assert_eq!(
            validate_password("line\n\tbreak"),
            Err(ConfigError::PasswordNonPrintable)
        );
        assert_eq!(
            validate_password(&"a".repeat(MAX_PASSWORD_BYTES + 1)),
            Err(ConfigError::PasswordTooLong)
        );
        let valid_boundary = format!(" {}~", "a".repeat(MAX_PASSWORD_BYTES - 2));
        assert_eq!(valid_boundary.len(), MAX_PASSWORD_BYTES);
        assert!(validate_password(&valid_boundary).is_ok());

        let mut model = ConfigurationModel::new();
        model
            .stage_password(
                "NEC-boundary-code".into(),
                String::new(),
                secret(&valid_boundary),
            )
            .expect("a 63-byte printable ASCII password should be accepted");
    }

    #[test]
    fn empty_control_scan_does_not_create_pending_update() {
        let mut model = ConfigurationModel::new();
        model.stage_control_code(ControlName::Unlock, "NEC-existing-code".into());
        model.mark_applied();
        assert!(pending_control_from_scan(ControlName::Unlock, String::new()).is_none());
        assert!(!model.dirty);
        assert_eq!(
            model.staged.control_code(ControlName::Unlock),
            "NEC-existing-code"
        );

        let pending = pending_control_from_scan(ControlName::Unlock, "NEC-public-code".into());
        assert_eq!(
            pending.map(|(_, code)| code),
            Some("NEC-public-code".into())
        );
        assert!(!model.dirty);
    }

    #[test]
    fn model_stages_public_fields_without_cloneable_secrets() {
        let mut model = ConfigurationModel::new();
        model
            .stage_password(
                "NEC-public-code".to_string(),
                "Windows".to_string(),
                secret("fixture-password-not-output"),
            )
            .expect("first slot should be accepted");
        assert_eq!(model.staged.password_count(), 1);
        assert_eq!(model.staged.passwords[0].keycode(), "NEC-public-code");
        assert_eq!(model.staged.passwords[0].alias(), "Windows");
        assert!(model.dirty);
    }

    #[test]
    fn model_rejects_duplicate_and_seventeenth_slots() {
        let mut model = ConfigurationModel::new();
        model
            .stage_password(
                "NEC-duplicate".to_string(),
                String::new(),
                secret("fixture-secret"),
            )
            .expect("first slot should be accepted");
        assert_eq!(
            model.stage_password(
                "NEC-duplicate".to_string(),
                String::new(),
                secret("fixture-secret"),
            ),
            Err(ConfigError::DuplicateKeycode)
        );

        for index in 0..(SLOT_CAPACITY - 1) {
            model
                .stage_password(
                    format!("NEC-full-slot-{index}"),
                    String::new(),
                    secret("fixture-secret"),
                )
                .expect("slot should be accepted");
        }
        assert_eq!(
            model.stage_password(
                "NEC-seventeenth".to_string(),
                String::new(),
                secret("fixture-secret"),
            ),
            Err(ConfigError::SlotCapacity)
        );
    }

    #[test]
    fn selectable_lines_have_a_single_leading_cursor() {
        assert_eq!(selectable_line(true, "Apply"), "> Apply");
        assert_eq!(selectable_line(false, "Cancel"), "  Cancel");
    }

    #[test]
    fn raw_mode_lines_use_crlf_instead_of_bare_lf() {
        let mut output = Vec::new();
        write_terminal_line(&mut output, "Main menu").expect("line should render");
        write_terminal_line(&mut output, "> Exit").expect("line should render");
        assert_eq!(output, b"Main menu\r\n> Exit\r\n");
        assert!(output
            .iter()
            .enumerate()
            .all(|(index, byte)| *byte != b'\n' || (index > 0 && output[index - 1] == b'\r')));
    }

    #[test]
    fn placeholder_output_contains_no_secret_or_secret_length() {
        let mut configuration = Configuration::new();
        configuration
            .add_password(PasswordEntry::new(
                "NEC-public-code".to_string(),
                "Linux".to_string(),
                secret("fixture-password-not-output"),
            ))
            .expect("fixture should be accepted");
        assert!(!configuration.passwords[0].secret_bytes().is_empty());

        let output_path = std::env::temp_dir().join(format!(
            "ad00020-config-tool-test-{}-{}.json",
            std::process::id(),
            std::thread::current().name().unwrap_or("test")
        ));
        apply_config(&configuration, &output_path).expect("placeholder should write");
        let mut output = String::new();
        fs::File::open(&output_path)
            .expect("output should exist")
            .read_to_string(&mut output)
            .expect("output should be readable");
        assert!(!output.contains("fixture-password-not-output"));
        assert!(!output.contains("secret_length"));
        assert!(!output.contains("password_length"));
        assert!(output.contains("secret_values_written"));
        let _ = fs::remove_file(output_path);
    }

    #[test]
    fn ctrl_d_is_not_a_commit_action() {
        let key = KeyEvent::new(KeyCode::Char('d'), KeyModifiers::CONTROL);
        assert!(!is_cancel_key(key));
    }
}
