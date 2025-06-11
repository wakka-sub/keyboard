using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.IO.Ports;
using System.Linq;
using System.Management;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Threading;
using Windows.Media.Control;    // Media Session API
using Kawazu;
using System.Windows.Forms; // NotifyIconを使用するために追加
using System.Drawing;       // ★ REMOVED: Properties.Resources経由でIcon型を直接使えるため、明示的なusingは不要な場合が多い

namespace KeymapperGui
{
    public class KeyMappingViewModel : INotifyPropertyChanged
    {
        // ... (このクラスに変更はありません) ...
        public event PropertyChangedEventHandler PropertyChanged;
        protected void OnPropertyChanged(string propertyName) => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));

        public string Label { get; set; }
        public int MapIndex { get; set; }
        public ObservableCollection<string> AssignmentTypes { get; } = new ObservableCollection<string> { "Unassigned", "Key Input", "Special Key", "Command" };

        private string _selectedType = "Unassigned";
        public string SelectedType
        {
            get => _selectedType;
            set
            {
                if (_selectedType != value)
                {
                    _selectedType = value;
                    OnPropertyChanged(nameof(SelectedType));
                    UpdateVisibility();
                }
            }
        }

        private string _keyCombinationText = "(Unassigned)";
        public string KeyCombinationText
        {
            get => _keyCombinationText;
            set
            {
                if (_keyCombinationText != value)
                {
                    _keyCombinationText = value;
                    OnPropertyChanged(nameof(KeyCombinationText));
                }
            }
        }

        private HidKeyCodes.HidMapping _selectedSpecialKey;
        public HidKeyCodes.HidMapping SelectedSpecialKey
        {
            get => _selectedSpecialKey;
            set
            {
                if (_selectedSpecialKey != value)
                {
                    _selectedSpecialKey = value;
                    OnPropertyChanged(nameof(SelectedSpecialKey));
                    UpdateFromSpecialKey();
                }
            }
        }

        private string _commandText;
        public string CommandText
        {
            get => _commandText;
            set
            {
                if (_commandText != value)
                {
                    _commandText = value;
                    OnPropertyChanged(nameof(CommandText));
                }
            }
        }

        public bool IsKeyType => SelectedType == "Key Input";
        public bool IsSpecialKeyType => SelectedType == "Special Key";
        public bool IsCommandType => SelectedType == "Command";
        public byte Type { get; set; }
        public ushort[] Codes { get; set; } = new ushort[4];

        private void UpdateVisibility()
        {
            OnPropertyChanged(nameof(IsKeyType));
            OnPropertyChanged(nameof(IsSpecialKeyType));
            OnPropertyChanged(nameof(IsCommandType));

            if (SelectedType != "Key Input") KeyCombinationText = "(Unassigned)";
            if (SelectedType != "Special Key")
            {
                _selectedSpecialKey = null;
                OnPropertyChanged(nameof(SelectedSpecialKey));
            }
            if (SelectedType == "Unassigned")
            {
                Type = 0;
                Array.Clear(Codes, 0, Codes.Length);
                CommandText = "";
            }
        }

        private void UpdateFromSpecialKey()
        {
            if (SelectedSpecialKey == null || SelectedSpecialKey.Code == 0) return;
            Type = 2;
            Array.Clear(Codes, 0, Codes.Length);
            Codes[0] = SelectedSpecialKey.Code;
            KeyCombinationText = SelectedSpecialKey.Name;
        }
    }

    public partial class MainWindow : Window
    {
        // ... (定数やフィールドの定義に変更はありません) ...
        private const int MAX_COMBO_KEYS = 4;
        private const int NUM_TOTAL_MAPS = 13;
        private const string COMMANDS_FILE_PATH = "commands.json";
        private const string TARGET_VID = "4545";
        private const string TARGET_PID = "4545";

        private readonly SerialPort _serialPort = new SerialPort();
        private readonly StringBuilder _receivedData = new StringBuilder();
        private readonly List<Key> _sessionKeys = new List<Key>();

        private GlobalSystemMediaTransportControlsSessionManager _mediaManager;
        private string _lastSongInfo = null;
        private readonly DispatcherTimer _mediaTimer = new DispatcherTimer();
        private KawazuConverter converter = new KawazuConverter();

        private TaskCompletionSource<bool> _songInfoAckTcs;
        private readonly object _ackLock = new object();

        private NotifyIcon _notifyIcon;
        private bool _isExplicitlyClosing = false;

        public ObservableCollection<KeyMappingViewModel> KeyMappings { get; set; } = new ObservableCollection<KeyMappingViewModel>();

        public MainWindow()
        {
            InitializeComponent();
            InitializeKeyMappings();
            LoadCommandConfig();
            DataContext = this;
            KeyMappingItemsControl.ItemsSource = KeyMappings;

            _serialPort.DataReceived += SerialPort_DataReceived;
            Loaded += MainWindow_Loaded;

            InitializeNotifyIcon();

            TryAutoConnect();
        }

        // ★★★ MODIFIED: アイコンの読み込み方法を .resx 対応に修正 ★★★
        private void InitializeNotifyIcon()
        {
            _notifyIcon = new NotifyIcon();
            _notifyIcon.Text = "Keymapper GUI"; // アイコンにマウスを合わせたときのツールチップ
            _notifyIcon.Visible = true;

            // .resxリソースからアイコンを直接読み込みます。
            // これにより、pack URIを使った複雑な読み込みやtry-catchが不要になります。
            // プロジェクト名が `KeymapperGui` の場合、リソースは `KeymapperGui.Properties.Resources` に含まれます。
            // usingディレクティブを追加するか、完全修飾名でアクセスします。
            byte[] iconBytes = keyboard.Properties.Resources.app_icon;

            using (var ms = new MemoryStream(iconBytes))
            {
                _notifyIcon.Icon = new Icon(ms);
            }

            // アイコンのダブルクリックイベント
            _notifyIcon.DoubleClick += (s, args) => ShowWindow();

            // コンテキストメニューの作成
            _notifyIcon.ContextMenuStrip = new ContextMenuStrip();
            _notifyIcon.ContextMenuStrip.Items.Add("Show", null, (s, e) => ShowWindow());
            _notifyIcon.ContextMenuStrip.Items.Add("Exit", null, (s, e) => ExitApplication());
        }

        private void ShowWindow()
        {
            this.Show();
            this.WindowState = WindowState.Normal;
            this.Activate();
        }

        private void ExitApplication()
        {
            _isExplicitlyClosing = true;
            this.Close();
        }

        protected override void OnClosing(CancelEventArgs e)
        {
            if (_isExplicitlyClosing)
            {
                _notifyIcon.Dispose();
                CloseSerialPort();
                base.OnClosing(e);
            }
            else
            {
                e.Cancel = true;
                this.Hide();
            }
        }


        // ... (以降のコードには変更はありません) ...
        private void InitializeKeyMappings()
        {
            string[] labels = {
                "Key 1:", "Key 2:", "Key 3:", "Key 4:", "Key 5:", "Key 6:", "Key 7:", "Key 8:",
                "Encoder CW:", "Encoder CCW:", "Encoder SW:",
                "Encoder SW+CW:", "Encoder SW+CCW:"
            };
            for (int i = 0; i < NUM_TOTAL_MAPS; i++)
            {
                KeyMappings.Add(new KeyMappingViewModel { Label = labels[i], MapIndex = i });
            }
        }

        private async void MainWindow_Loaded(object sender, RoutedEventArgs e)
        {
            _mediaManager = await GlobalSystemMediaTransportControlsSessionManager.RequestAsync();
            _mediaTimer.Interval = TimeSpan.FromMilliseconds(500);
            _mediaTimer.Tick += async (_, __) => await CheckAndSendMediaInfo();
            _mediaTimer.Start();
        }

        private async Task CheckAndSendMediaInfo()
        {
            if (_songInfoAckTcs != null) return;

            TaskCompletionSource<bool> tcs = null;
            try
            {
                string songInfoPayload;
                var session = _mediaManager.GetSessions()
                    .FirstOrDefault(s => s.GetPlaybackInfo().PlaybackStatus != GlobalSystemMediaTransportControlsSessionPlaybackStatus.Closed);

                if (session == null)
                {
                    songInfoPayload = "Waiting for the beat...,0,0,0";
                }
                else
                {
                    var props = await session.TryGetMediaPropertiesAsync();
                    string title = props.Title;
                    if (string.IsNullOrWhiteSpace(title))
                    {
                        title = "Waiting for the beat...";
                    }

                    var playbackInfo = session.GetPlaybackInfo();
                    string status = playbackInfo.PlaybackStatus == GlobalSystemMediaTransportControlsSessionPlaybackStatus.Playing ? "1" : "0";

                    var timelineProps = session.GetTimelineProperties();
                    long positionMs = (long)timelineProps.Position.TotalMilliseconds;
                    long durationMs = (long)timelineProps.EndTime.TotalMilliseconds;

                    string romajiFull = await converter.Convert(title, To.Romaji, Mode.Spaced, RomajiSystem.Hepburn, "(", ")");
                    int maxLength = 50;
                    string filtered = new string(romajiFull
                        .Where(c => c >= 0x20 && c <= 0x7E)
                        .ToArray());
                    if (filtered.Length > maxLength)
                        filtered = filtered.Substring(0, maxLength);

                    songInfoPayload = $"{filtered.Replace(",", "")},{status},{positionMs},{durationMs}";
                }

                if (songInfoPayload == _lastSongInfo) return;

                if (_serialPort.IsOpen)
                {
                    lock (_ackLock)
                    {
                        _songInfoAckTcs = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
                        tcs = _songInfoAckTcs;
                    }

                    _serialPort.WriteLine($"SONG_INFO:{songInfoPayload}");
                    Debug.WriteLine($"Sent SONG_INFO, awaiting ACK: {songInfoPayload}");

                    var timeoutTask = Task.Delay(1000);
                    var completedTask = await Task.WhenAny(tcs.Task, timeoutTask);

                    if (completedTask == tcs.Task && await tcs.Task)
                    {
                        _lastSongInfo = songInfoPayload;
                        Debug.WriteLine("SONG_INFO acknowledged successfully.");
                    }
                    else
                    {
                        Debug.WriteLine("SONG_INFO acknowledgment timed out.");
                    }
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"MediaInfo Error: {ex.Message}");
            }
            finally
            {
                lock (_ackLock)
                {
                    _songInfoAckTcs = null;
                }
            }
        }

        #region UI: Key Input Logic

        private void KeyTextBox_GotFocus(object sender, RoutedEventArgs e) => _sessionKeys.Clear();
        private void KeyTextBox_LostFocus(object sender, RoutedEventArgs e) => _sessionKeys.Clear();

        private void KeyTextBox_PreviewKeyDown(object sender, System.Windows.Input.KeyEventArgs e)
        {
            e.Handled = true;
            if (!(sender is System.Windows.Controls.TextBox tb) || !(tb.DataContext is KeyMappingViewModel vm) || vm.SelectedType != "Key Input")
            {
                _sessionKeys.Clear();
                return;
            }

            Key key = (e.Key == Key.System) ? e.SystemKey : e.Key;
            if (key == Key.Back && _sessionKeys.Any())
                _sessionKeys.RemoveAt(_sessionKeys.Count - 1);
            else if (!_sessionKeys.Contains(key) && _sessionKeys.Count < MAX_COMBO_KEYS)
                _sessionKeys.Add(key);

            UpdateKeyCombination(vm);
        }

        private void UpdateKeyCombination(KeyMappingViewModel vm)
        {
            var hidMappings = _sessionKeys
                .Select(k => HidKeyCodes.GetHidMapping(k))
                .Where(m => m != null && m.Code != 0)
                .GroupBy(m => m.Code).Select(g => g.First())
                .OrderBy(m => (m.Code >= 0xE0 && m.Code <= 0xE7) ? 0 : 1)
                .ThenBy(m => m.Code)
                .ToList();

            if (!hidMappings.Any())
            {
                vm.KeyCombinationText = "(Unassigned)";
                vm.Type = 0;
                Array.Clear(vm.Codes, 0, vm.Codes.Length);
                return;
            }

            vm.KeyCombinationText = string.Join(" + ", hidMappings.Select(m => m.Name));
            vm.Type = 1;
            Array.Clear(vm.Codes, 0, vm.Codes.Length);
            for (int i = 0; i < Math.Min(vm.Codes.Length, hidMappings.Count); i++)
                vm.Codes[i] = hidMappings[i].Code;
        }

        private void ClearButton_Click(object sender, RoutedEventArgs e)
        {
            if ((sender as FrameworkElement)?.DataContext is KeyMappingViewModel vm)
            {
                _sessionKeys.Clear();
                UpdateKeyCombination(vm);
                vm.SelectedType = "Unassigned";
            }
        }

        #endregion

        #region Serial Communication & Config Handling

        private void SerialPort_DataReceived(object sender, SerialDataReceivedEventArgs e)
        {
            try
            {
                string data = _serialPort.ReadExisting();
                _receivedData.Append(data);
                string buf = _receivedData.ToString();
                int idx;
                while ((idx = buf.IndexOf('\n')) >= 0)
                {
                    string line = buf.Substring(0, idx).Trim();
                    _receivedData.Remove(0, idx + 1);
                    if (!string.IsNullOrEmpty(line))
                        Dispatcher.Invoke(() => HandleResponse(line));
                }
            }
            catch (Exception ex) { Debug.WriteLine($"DataReceived Error: {ex.Message}"); }
        }

        private void HandleResponse(string resp)
        {
            if (resp == "OK")
            {
                TaskCompletionSource<bool> tcs;
                lock (_ackLock)
                {
                    tcs = _songInfoAckTcs;
                }

                if (tcs != null && tcs.TrySetResult(true))
                {
                    Debug.WriteLine("Received ACK for SONG_INFO.");
                    return;
                }

                StatusTextBlock.Text = "Configuration written successfully.";
            }
            else if (resp.StartsWith("CONFIG:"))
            {
                UpdateUIFromConfig(resp.Substring(7));
                StatusTextBlock.Text = "Configuration loaded successfully.";
            }
            else if (resp.StartsWith("CMD:") && int.TryParse(resp.Substring(4), out int idx))
            {
                ExecuteCommand(idx);
            }
        }

        private void ExecuteCommand(int mapIndex)
        {
            if (mapIndex < 0 || mapIndex >= KeyMappings.Count) return;
            var vm = KeyMappings[mapIndex];
            var text = vm.CommandText?.Trim();
            if (vm.SelectedType != "Command" || string.IsNullOrWhiteSpace(text))
                return;

            try
            {
                StatusTextBlock.Text = $"Executing: {text}";

                // 1) 実行ファイル名と引数を分離
                string fileName, args;
                var firstSpace = text.IndexOf(' ');
                if (firstSpace < 0)
                {
                    fileName = text;
                    args = "";
                }
                else
                {
                    fileName = text.Substring(0, firstSpace);
                    args = text.Substring(firstSpace + 1);
                }

                var psi = new ProcessStartInfo
                {
                    FileName = fileName,     // 例: "powershell" や "myapp.exe"
                    Arguments = args,         // 例: "-NoProfile -Command \"...\""
                    UseShellExecute = true,
                };

                Process.Start(psi);
            }
            catch (Exception ex)
            {
                System.Windows.MessageBox.Show(
                    $"Error executing '{vm.CommandText}': {ex.Message}",
                    "Command Error",
                    MessageBoxButton.OK,
                    MessageBoxImage.Error);
                StatusTextBlock.Text = "Error executing command.";
            }
        }


        private void UpdateUIFromConfig(string data)
        {
            var parts = data.Split(',');
            const int valsPer = 1 + MAX_COMBO_KEYS;
            for (int i = 0; i < KeyMappings.Count; i++)
            {
                int baseIdx = i * valsPer;
                if (baseIdx >= parts.Length) break;
                if (!byte.TryParse(parts[baseIdx], out byte t)) continue;

                var vm = KeyMappings[i];
                vm.Type = t;
                var codes = new List<ushort>();
                for (int j = 0; j < MAX_COMBO_KEYS; j++)
                    if (ushort.TryParse(parts[baseIdx + 1 + j], out ushort c) && c != 0)
                        codes.Add(c);

                Array.Clear(vm.Codes, 0, vm.Codes.Length);
                for (int k = 0; k < codes.Count; k++)
                    vm.Codes[k] = codes[k];

                switch (vm.Type)
                {
                    case 1:
                        vm.SelectedType = "Key Input";
                        var sorted = codes.OrderBy(c => (c >= 0xE0 && c <= 0xE7) ? 0 : 1).ThenBy(c => c);
                        vm.KeyCombinationText = string.Join(" + ",
                            sorted.Select(c => HidKeyCodes.GetMappingName(vm.Type, c)).Where(n => !n.StartsWith("Unknown")));
                        break;
                    case 2:
                        vm.SelectedType = "Special Key";
                        vm.SelectedSpecialKey = HidKeyCodes.SpecialKeys.FirstOrDefault(m => m.Code == codes.FirstOrDefault());
                        break;
                    case 3:
                        vm.SelectedType = "Command";
                        vm.KeyCombinationText = "(Command)";
                        break;
                    default:
                        vm.SelectedType = "Unassigned";
                        break;
                }
            }
        }

        private void ReadButton_Click(object sender, RoutedEventArgs e)
        {
            SendCommand("GET_CONFIG");
            StatusTextBlock.Text = "Reading configuration from device...";
        }

        private void WriteButton_Click(object sender, RoutedEventArgs e)
        {
            var parts = KeyMappings.Select(vm =>
            {
                if (vm.SelectedType == "Command") vm.Type = 3;
                else if (vm.SelectedType == "Unassigned") vm.Type = 0;
                var codes = string.Join(",", vm.Codes.Select(c => c.ToString()));
                return $"{vm.Type},{codes}";
            });
            string cmd = $"SET_CONFIG:{string.Join(",", parts)}";
            SendCommand(cmd);
            SaveCommandConfig();
        }

        private void SendCommand(string command)
        {
            if (_serialPort.IsOpen)
            {
                try { _serialPort.WriteLine(command); Debug.WriteLine($"Sent: {command}"); }
                catch (Exception ex) { System.Windows.MessageBox.Show($"Failed to send command: {ex.Message}", "Communication Error"); }
            }
        }

        private void SaveCommandConfig()
        {
            var dict = KeyMappings
                .Where(vm => vm.SelectedType == "Command")
                .ToDictionary(vm => vm.MapIndex.ToString(), vm => vm.CommandText);
            try
            {
                var opts = new JsonSerializerOptions { WriteIndented = true };
                File.WriteAllText(COMMANDS_FILE_PATH, JsonSerializer.Serialize(dict, opts));
            }
            catch (Exception ex)
            {
                System.Windows.MessageBox.Show($"Failed to save config: {ex.Message}", "File Save Error");
            }
        }

        private void LoadCommandConfig()
        {
            if (!File.Exists(COMMANDS_FILE_PATH)) return;
            try
            {
                var dict = JsonSerializer.Deserialize<Dictionary<string, string>>(File.ReadAllText(COMMANDS_FILE_PATH));
                foreach (var kv in dict)
                {
                    if (int.TryParse(kv.Key, out int idx) && idx >= 0 && idx < KeyMappings.Count)
                        KeyMappings[idx].CommandText = kv.Value;
                }
            }
            catch (Exception ex)
            {
                System.Windows.MessageBox.Show($"Failed to load config: {ex.Message}", "File Load Error");
            }
        }

        #endregion

        #region Connection Management

        private string FindComPortByVidPid(string vid, string pid)
        {
            string pattern = $"VID_{vid}&PID_{pid}";
            try
            {
                var searcher = new ManagementObjectSearcher("root\\CIMV2",
                    $"SELECT * FROM Win32_PnPEntity WHERE DeviceID LIKE '%{pattern}%'");
                foreach (ManagementObject obj in searcher.Get())
                {
                    string name = obj["Name"]?.ToString();
                    var m = Regex.Match(name ?? "", @"\(COM\d+\)");
                    if (m.Success) return m.Value.Trim('(', ')');
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"WMI Error: {ex}");
            }
            return null;
        }

        private void TryAutoConnect()
        {
            StatusTextBlock.Text = $"Searching for device (VID:{TARGET_VID}, PID:{TARGET_PID})...";
            RefreshComPorts();
            var port = FindComPortByVidPid(TARGET_VID, TARGET_PID);
            if (!string.IsNullOrEmpty(port) && ComPortComboBox.Items.Contains(port))
            {
                StatusTextBlock.Text = $"Device found on {port}. Connecting...";
                ComPortComboBox.SelectedItem = port;
                Connect();
            }
            else
            {
                StatusTextBlock.Text = "Device not found. Please select a port manually.";
            }
        }

        private void ConnectButton_Click(object sender, RoutedEventArgs e) => Connect();
        private void DisconnectButton_Click(object sender, RoutedEventArgs e) => CloseSerialPort();
        private void RefreshButton_Click(object sender, RoutedEventArgs e) => RefreshComPorts();

        private void Connect()
        {
            if (ComPortComboBox.SelectedItem == null)
            {
                System.Windows.MessageBox.Show("Please select a COM port.", "Error");
                return;
            }
            try
            {
                if (_serialPort.IsOpen) _serialPort.Close();
                _serialPort.PortName = ComPortComboBox.SelectedItem.ToString();
                _serialPort.BaudRate = 115200;
                _serialPort.DtrEnable = true;
                _serialPort.RtsEnable = true;
                _serialPort.Open();
                SetUiConnectedState(true);
                StatusTextBlock.Text = $"Connected to {_serialPort.PortName}";
                ReadButton_Click(null, null);
            }
            catch (Exception ex)
            {
                System.Windows.MessageBox.Show($"Failed to connect: {ex.Message}", "Connection Error");
            }
        }

        private void RefreshComPorts()
        {
            var prev = ComPortComboBox.SelectedItem as string;
            ComPortComboBox.ItemsSource = SerialPort.GetPortNames();
            if (ComPortComboBox.Items.Count > 0)
            {
                ComPortComboBox.SelectedItem = ComPortComboBox.Items.Contains(prev) ? prev : ComPortComboBox.Items[0];
            }
        }

        private void SetUiConnectedState(bool isConnected)
        {
            ConnectButton.IsEnabled = !isConnected;
            DisconnectButton.IsEnabled = isConnected;
            RefreshButton.IsEnabled = !isConnected;
            ComPortComboBox.IsEnabled = !isConnected;
            KeyMappingItemsControl.IsEnabled = isConnected;
            ReadButton.IsEnabled = isConnected;
            WriteButton.IsEnabled = isConnected;
        }

        private void CloseSerialPort()
        {
            if (_serialPort.IsOpen) _serialPort.Close();
            SetUiConnectedState(false);
            StatusTextBlock.Text = "Disconnected";
        }

        #endregion
    }
}
