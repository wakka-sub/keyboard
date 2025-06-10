using System.Collections.Generic;
using System.Linq;
using System.Windows.Input;

namespace KeymapperGui // ご自身のプロジェクト名に合わせてください
{
    /// <summary>
    /// USB HIDキーコードとWPFのKey、表示名をマッピングする静的クラス。
    /// </summary>
    public static class HidKeyCodes
    {
        // HIDマッピング情報を保持するレコード
        public record HidMapping(string Name, byte Type, ushort Code);

        // WPFのKeyからHIDマッピングへの変換テーブル
        private static readonly Dictionary<Key, HidMapping> KeyToHidMap;
        // HIDコード(Type, Code)から表示名への逆引きテーブル
        private static readonly Dictionary<(byte, ushort), string> HidToNameMap;
        // ドロップダウンリスト用の特殊キーリスト
        public static List<HidMapping> SpecialKeys { get; private set; }

        static HidKeyCodes()
        {
            var mappings = new List<HidMapping>
            {
                // --- Type 1: Keyboard/Keypad Page (0x07) ---
                new("Ctrl", 1, 0xE0), new("Shift", 1, 0xE1), new("Alt", 1, 0xE2), new("Win", 1, 0xE3),
                new("Right Ctrl", 1, 0xE4), new("Right Shift", 1, 0xE5), new("Right Alt", 1, 0xE6), new("Right Win", 1, 0xE7),
                new("A", 1, 0x04), new("B", 1, 0x05), new("C", 1, 0x06), new("D", 1, 0x07), new("E", 1, 0x08), new("F", 1, 0x09), new("G", 1, 0x0A), new("H", 1, 0x0B), new("I", 1, 0x0C), new("J", 1, 0x0D), new("K", 1, 0x0E), new("L", 1, 0x0F), new("M", 1, 0x10), new("N", 1, 0x11), new("O", 1, 0x12), new("P", 1, 0x13), new("Q", 1, 0x14), new("R", 1, 0x15), new("S", 1, 0x16), new("T", 1, 0x17), new("U", 1, 0x18), new("V", 1, 0x19), new("W", 1, 0x1A), new("X", 1, 0x1B), new("Y", 1, 0x1C), new("Z", 1, 0x1D),
                new("1", 1, 0x1E), new("2", 1, 0x1F), new("3", 1, 0x20), new("4", 1, 0x21), new("5", 1, 0x22), new("6", 1, 0x23), new("7", 1, 0x24), new("8", 1, 0x25), new("9", 1, 0x26), new("0", 1, 0x27),
                new("F1", 1, 0x3A), new("F2", 1, 0x3B), new("F3", 1, 0x3C), new("F4", 1, 0x3D), new("F5", 1, 0x3E), new("F6", 1, 0x3F), new("F7", 1, 0x40), new("F8", 1, 0x41), new("F9", 1, 0x42), new("F10", 1, 0x43), new("F11", 1, 0x44), new("F12", 1, 0x45),
                new("F13", 1, 0x68), new("F14", 1, 0x69), new("F15", 1, 0x6A), new("F16", 1, 0x6B), new("F17", 1, 0x6C), new("F18", 1, 0x6D), new("F19", 1, 0x6E), new("F20", 1, 0x6F), new("F21", 1, 0x70), new("F22", 1, 0x71), new("F23", 1, 0x72), new("F24", 1, 0x73),
                new("Enter", 1, 0x28), new("Escape", 1, 0x29), new("Backspace", 1, 0x2A), new("Tab", 1, 0x2B), new("Space", 1, 0x2C),
                new("Insert", 1, 0x49), new("Delete", 1, 0x4C), new("Home", 1, 0x4A), new("End", 1, 0x4D), new("PageUp", 1, 0x4B), new("PageDown", 1, 0x4E),
                new("Right", 1, 0x4F), new("Left", 1, 0x50), new("Down", 1, 0x51), new("Up", 1, 0x52),
                new("CapsLock", 1, 0x39), new("NumLock", 1, 0x53), new("ScrollLock", 1, 0x47),
                new("-", 1, 0x2D), new("=", 1, 0x2E), new("[", 1, 0x2F), new("]", 1, 0x30), new("\\", 1, 0x31), new(";", 1, 0x33), new("'", 1, 0x34), new("`", 1, 0x35), new(",", 1, 0x36), new(".", 1, 0x37), new("/", 1, 0x38),
                new("Num /", 1, 0x54), new("Num *", 1, 0x55), new("Num -", 1, 0x56), new("Num +", 1, 0x57), new("Num Enter", 1, 0x58), new("Num 1", 1, 0x59), new("Num 2", 1, 0x5A), new("Num 3", 1, 0x5B), new("Num 4", 1, 0x5C), new("Num 5", 1, 0x5D), new("Num 6", 1, 0x5E), new("Num 7", 1, 0x5F), new("Num 8", 1, 0x60), new("Num 9", 1, 0x61), new("Num 0", 1, 0x62), new("Num .", 1, 0x63),
                new("PrintScreen", 1, 0x46), new("Pause", 1, 0x48), new("Menu", 1, 0x65),
                new("¥", 1, 0x87), new("Henkan", 1, 0x8A), new("Muhenkan", 1, 0x8B), new("Zenkaku/Hankaku", 1, 0x89), new("Katakana/Hiragana", 1, 0x88),

                // --- Type 2: Consumer Page (0x0C) ---
                new("Play/Pause", 2, 0xCD), new("Stop", 2, 0xB7), new("Next Track", 2, 0xB5), new("Prev Track", 2, 0xB6),
                new("Fast Forward", 2, 0xB3), new("Rewind", 2, 0xB4),
                new("Volume Up", 2, 0xE9), new("Volume Down", 2, 0xEA), new("Mute", 2, 0xE2),
                new("WWW Home", 2, 0x223), new("WWW Search", 2, 0x221), new("WWW Favorites", 2, 0x22A),
                new("WWW Refresh", 2, 0x227), new("WWW Stop", 2, 0x226), new("WWW Forward", 2, 0x224), new("WWW Back", 2, 0x225),
                new("Launch Mail", 2, 0x18A), new("Launch Media", 2, 0x183), new("Launch App 1", 2, 0x192), new("Launch App 2", 2, 0x194),
                new("Sleep", 2, 0x32), new("Power", 2, 0x30),
            };

            var wpfKeyMap = new Dictionary<Key, ushort>
            {
                { Key.LeftCtrl, 0xE0 }, { Key.RightCtrl, 0xE4 }, { Key.LeftShift, 0xE1 }, { Key.RightShift, 0xE5 },
                { Key.LeftAlt, 0xE2 }, { Key.RightAlt, 0xE6 }, { Key.LWin, 0xE3 }, { Key.RWin, 0xE7 },
                { Key.A, 0x04 }, { Key.B, 0x05 }, { Key.C, 0x06 }, { Key.D, 0x07 }, { Key.E, 0x08 }, { Key.F, 0x09 }, { Key.G, 0x0A }, { Key.H, 0x0B }, { Key.I, 0x0C }, { Key.J, 0x0D }, { Key.K, 0x0E }, { Key.L, 0x0F }, { Key.M, 0x10 }, { Key.N, 0x11 }, { Key.O, 0x12 }, { Key.P, 0x13 }, { Key.Q, 0x14 }, { Key.R, 0x15 }, { Key.S, 0x16 }, { Key.T, 0x17 }, { Key.U, 0x18 }, { Key.V, 0x19 }, { Key.W, 0x1A }, { Key.X, 0x1B }, { Key.Y, 0x1C }, { Key.Z, 0x1D },
                { Key.D1, 0x1E }, { Key.D2, 0x1F }, { Key.D3, 0x20 }, { Key.D4, 0x21 }, { Key.D5, 0x22 }, { Key.D6, 0x23 }, { Key.D7, 0x24 }, { Key.D8, 0x25 }, { Key.D9, 0x26 }, { Key.D0, 0x27 },
                { Key.F1, 0x3A }, { Key.F2, 0x3B }, { Key.F3, 0x3C }, { Key.F4, 0x3D }, { Key.F5, 0x3E }, { Key.F6, 0x3F }, { Key.F7, 0x40 }, { Key.F8, 0x41 }, { Key.F9, 0x42 }, { Key.F10, 0x43 }, { Key.F11, 0x44 }, { Key.F12, 0x45 },
                { Key.F13, 0x68 }, { Key.F14, 0x69 }, { Key.F15, 0x6A }, { Key.F16, 0x6B }, { Key.F17, 0x6C }, { Key.F18, 0x6D }, { Key.F19, 0x6E }, { Key.F20, 0x6F }, { Key.F21, 0x70 }, { Key.F22, 0x71 }, { Key.F23, 0x72 }, { Key.F24, 0x73 },
                { Key.Enter, 0x28 }, { Key.Escape, 0x29 }, { Key.Back, 0x2A }, { Key.Tab, 0x2B }, { Key.Space, 0x2C },
                { Key.Insert, 0x49 }, { Key.Delete, 0x4C }, { Key.Home, 0x4A }, { Key.End, 0x4D }, { Key.PageUp, 0x4B }, { Key.PageDown, 0x4E },
                { Key.Right, 0x4F }, { Key.Left, 0x50 }, { Key.Down, 0x51 }, { Key.Up, 0x52 },
                { Key.CapsLock, 0x39 }, { Key.NumLock, 0x53 }, { Key.Scroll, 0x47 },
                { Key.OemMinus, 0x2D }, { Key.OemPlus, 0x2E }, { Key.OemOpenBrackets, 0x2F }, { Key.OemCloseBrackets, 0x30 },
                { Key.Oem5, 0x31 }, { Key.Oem1, 0x33 }, { Key.OemQuotes, 0x34 }, { Key.Oem3, 0x35 },
                { Key.OemComma, 0x36 }, { Key.OemPeriod, 0x37 }, { Key.Oem2, 0x38 },
                { Key.PrintScreen, 0x46 }, { Key.Pause, 0x48 }, { Key.Apps, 0x65 },
                { Key.NumPad0, 0x62 }, { Key.NumPad1, 0x59 }, { Key.NumPad2, 0x5A }, { Key.NumPad3, 0x5B }, { Key.NumPad4, 0x5C }, { Key.NumPad5, 0x5D }, { Key.NumPad6, 0x5E }, { Key.NumPad7, 0x5F }, { Key.NumPad8, 0x60 }, { Key.NumPad9, 0x61 },
                { Key.Divide, 0x54 }, { Key.Multiply, 0x55 }, { Key.Subtract, 0x56 }, { Key.Add, 0x57 }, { Key.Decimal, 0x63 },
                { Key.MediaNextTrack, 0xB5 }, { Key.MediaPreviousTrack, 0xB6 }, { Key.MediaStop, 0xB7 }, { Key.MediaPlayPause, 0xCD },
                { Key.VolumeMute, 0xE2 }, { Key.VolumeDown, 0xEA }, { Key.VolumeUp, 0xE9 },
                { Key.BrowserBack, 0x225 }, { Key.BrowserForward, 0x224 }, { Key.BrowserRefresh, 0x227 }, { Key.BrowserStop, 0x226 }, { Key.BrowserSearch, 0x221 }, { Key.BrowserFavorites, 0x22A }, { Key.BrowserHome, 0x223 },
                { Key.LaunchMail, 0x18A }, { Key.SelectMedia, 0x183 }, { Key.LaunchApplication1, 0x192 }, { Key.LaunchApplication2, 0x194 },
                { Key.Sleep, 0x32 },
                { Key.ImeConvert, 0x8A }, { Key.ImeNonConvert, 0x8B }, { Key.ImeAccept, 0x88 }, { Key.Oem8, 0x89 },
            };

            HidToNameMap = mappings.GroupBy(m => (m.Type, m.Code)).ToDictionary(g => g.Key, g => g.First().Name);

            KeyToHidMap = new Dictionary<Key, HidMapping>();
            foreach (var pair in wpfKeyMap)
            {
                if (HidToNameMap.TryGetValue((1, pair.Value), out var name))
                    KeyToHidMap[pair.Key] = new HidMapping(name, 1, pair.Value);
                else if (HidToNameMap.TryGetValue((2, pair.Value), out name))
                    KeyToHidMap[pair.Key] = new HidMapping(name, 2, pair.Value);
            }

            var specialKeyListWithPrompt = new List<HidMapping> { new("-- Select Special Key --", 0, 0) };
            var sortedSpecialKeys = mappings
                .Where(m => m.Type == 2 || (m.Type == 1 && m.Code >= 0x68 && m.Code <= 0x73) || new ushort[] { 0x46, 0x48, 0x39, 0x53, 0x47, 0x65, 0x87, 0x8A, 0x8B, 0x89, 0x88 }.Contains(m.Code))
                .OrderBy(m => m.Type).ThenBy(m => m.Name)
                .ToList();
            specialKeyListWithPrompt.AddRange(sortedSpecialKeys);
            SpecialKeys = specialKeyListWithPrompt;
        }

        public static HidMapping GetHidMapping(Key key)
        {
            return KeyToHidMap.TryGetValue(key, out var mapping) ? mapping : null;
        }

        public static string GetMappingName(byte type, ushort code)
        {
            if (type == 0 && code == 0) return "(Unassigned)";
            return HidToNameMap.TryGetValue((type, code), out var name) ? name : $"Unknown(T:{type},C:{code})";
        }
    }
}