using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;
using System.Windows.Forms;

[assembly: System.Reflection.AssemblyTitle("Scrap Mechanic VR Held Item Calibrator")]
[assembly: System.Reflection.AssemblyDescription("Live grouped position and rotation calibration for every VR held item")]
[assembly: System.Reflection.AssemblyVersion("1.0.0.0")]
[assembly: System.Reflection.AssemblyFileVersion("1.0.0.0")]

namespace ScrapMechanicVRHeldCalibration
{
    internal sealed class Pose
    {
        internal decimal X, Y, Z, Pitch, Yaw, Roll, Scale;
        internal Pose Clone() { return (Pose)MemberwiseClone(); }
    }

    internal sealed class ProfileDef
    {
        internal readonly string Section, Name, Category;
        internal readonly Pose Default;
        internal ProfileDef(string section, string name, string category, decimal x, decimal y,
            decimal z, decimal scale)
        {
            Section = section; Name = name; Category = category;
            Default = new Pose { X = x, Y = y, Z = z, Scale = scale };
        }
        public override string ToString() { return Name; }
    }

    internal sealed class PoseRow
    {
        internal string Key;
        internal NumericUpDown Number;
        internal TrackBar Slider;
        internal decimal SliderFactor;
    }

    internal sealed class CalibrationForm : Form
    {
        private static readonly Color Ink = Color.FromArgb(30, 34, 39);
        private static readonly Color Panel = Color.FromArgb(42, 47, 53);
        private static readonly Color Panel2 = Color.FromArgb(52, 58, 64);
        private static readonly Color Accent = Color.FromArgb(255, 191, 32);
        private static readonly Color Muted = Color.FromArgb(170, 178, 186);

        private readonly string gameRoot = Directory.GetParent(AppDomain.CurrentDomain.BaseDirectory.TrimEnd(
            Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar)).FullName;
        private readonly string configPath;
        private readonly string statusPath;
        private readonly List<ProfileDef> profiles = new List<ProfileDef>();
        private readonly Dictionary<string, ProfileDef> bySection = new Dictionary<string, ProfileDef>(StringComparer.OrdinalIgnoreCase);
        private readonly Dictionary<string, Pose> poses = new Dictionary<string, Pose>(StringComparer.OrdinalIgnoreCase);
        private readonly Dictionary<string, TreeNode> nodes = new Dictionary<string, TreeNode>(StringComparer.OrdinalIgnoreCase);
        private readonly Dictionary<string, PoseRow> rows = new Dictionary<string, PoseRow>(StringComparer.OrdinalIgnoreCase);
        private readonly Timer saveTimer = new Timer();
        private readonly Timer statusTimer = new Timer();
        private readonly TreeView profileTree = new TreeView();
        private readonly Label activeItem = new Label();
        private readonly Label activeProfile = new Label();
        private readonly Label editorTitle = new Label();
        private readonly Label status = new Label();
        private readonly CheckBox followHeld = new CheckBox();
        private ProfileDef selected;
        private Pose clipboardPose;
        private bool loading;
        private DateTime statusWriteTime;

        internal CalibrationForm()
        {
            configPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "ScrapMechanicVR-HeldCalibration.ini");
            statusPath = Path.Combine(gameRoot, "Data", "NativeVR", "held_item_status.json");
            BuildProfiles();

            Text = "Scrap Mechanic VR - Live Held Item Calibrator";
            ClientSize = new Size(1040, 720);
            MinimumSize = new Size(920, 640);
            StartPosition = FormStartPosition.CenterScreen;
            Font = new Font("Segoe UI", 9.0f);
            BackColor = Ink;
            ForeColor = Color.White;
            Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath);

            BuildUi();
            LoadConfiguration();
            SelectProfile(profiles[0].Section, false);

            saveTimer.Interval = 45;
            saveTimer.Tick += delegate { saveTimer.Stop(); SaveConfiguration(); };
            statusTimer.Interval = 150;
            statusTimer.Tick += delegate { PollHeldStatus(); };
            statusTimer.Start();
        }

        private void BuildProfiles()
        {
            Add("Hammer", "Hammer", "Dedicated tools", 0.000m, -0.025m, -0.065m, 0.160m);
            Add("ConnectionTool", "Connection Tool", "Dedicated tools", -0.020m, -0.035m, -0.055m, 0.160m);
            Add("PaintTool", "Paint Tool", "Dedicated tools", -0.015m, -0.040m, -0.060m, 0.160m);
            Add("WeldTool", "Weld Tool", "Dedicated tools", -0.030m, -0.035m, -0.065m, 0.150m);
            Add("Spudgun", "Spudgun", "VR guns", -0.020m, -0.035m, -0.060m, 0.145m);
            Add("Shotgun", "Shotgun", "VR guns", -0.020m, -0.035m, -0.060m, 0.145m);
            Add("GatlingGun", "Gatling Gun", "VR guns", -0.020m, -0.035m, -0.060m, 0.145m);
            Add("ScrapSpudgun", "Scrap Spudgun", "VR guns", -0.020m, -0.035m, -0.060m, 0.145m);
            Add("PotatoLauncher", "Potato Launcher", "VR guns", -0.020m, -0.035m, -0.060m, 0.145m);
            Add("ClayGun", "Clay Gun", "VR guns", -0.122m, -0.031m, -0.172m, 0.145m);
            Add("Lift", "Lift", "Gameplay items", -0.010m, -0.030m, -0.060m, 0.190m);
            Add("Handbook", "Handbook", "Gameplay items", -0.010m, -0.045m, -0.100m, 0.130m);
            Add("Bucket", "Buckets", "Gameplay items", -0.015m, -0.060m, -0.090m, 0.110m);
            Add("Glowstick", "Glowstick", "Gameplay items", -0.010m, -0.030m, -0.045m, 0.160m);
            Add("Cornade", "Cornade", "Gameplay items", -0.015m, -0.035m, -0.060m, 0.085m);
            Add("LooseClay", "Loose Clay", "Gameplay items", -0.010m, -0.045m, -0.080m, 0.105m);
            Add("FireExtinguisher", "Fire Extinguisher", "Gameplay items", -0.020m, -0.045m, -0.075m, 0.115m);
            Add("SeedPlanter", "Seed Planter", "Gameplay items", -0.010m, -0.025m, -0.045m, 0.140m);
            Add("Fertilizer", "Fertilizer", "Gameplay items", -0.010m, -0.040m, -0.065m, 0.120m);
            Add("FoodAndDrink", "Food and Drink", "Gameplay items", -0.010m, -0.035m, -0.055m, 0.120m);
            Add("LongSandwich", "Long Sandwich", "Gameplay items", -0.010m, -0.040m, -0.085m, 0.075m);
            Add("SoilBag", "Soil Bag", "Gameplay items", -0.010m, -0.040m, -0.075m, 0.100m);
            Add("KeyItems", "Key Items", "Gameplay items", -0.010m, -0.030m, -0.045m, 0.130m);
            Add("PowerCore", "Power Core", "Gameplay items", -0.010m, -0.030m, -0.045m, 0.140m);
            Add("ResourceTool", "Resource Tool", "Gameplay items", -0.010m, -0.035m, -0.055m, 0.140m);
            Add("CarryItems", "Carry Items", "Gameplay items", -0.010m, -0.090m, -0.160m, 0.210m);
            Add("Logbook", "Logbook", "Gameplay items", -0.010m, -0.040m, -0.085m, 0.130m);
            Add("Blocks", "All Blocks", "Grouped blocks and parts", -0.010m, -0.040m, -0.085m, 0.075m);
            Add("Wedges", "All Wedges", "Grouped blocks and parts", -0.010m, -0.040m, -0.085m, 0.075m);
            Add("SmallParts", "Small Parts", "Grouped blocks and parts", -0.010m, -0.040m, -0.085m, 0.060m);
            Add("MediumParts", "Medium Parts", "Grouped blocks and parts", -0.010m, -0.045m, -0.095m, 0.075m);
            Add("LargeParts", "Large Parts", "Grouped blocks and parts", -0.010m, -0.055m, -0.115m, 0.095m);
            Add("Consumables", "Consumables", "Grouped blocks and parts", -0.010m, -0.040m, -0.075m, 0.070m);
            Add("Resources", "Resources", "Grouped blocks and parts", -0.010m, -0.045m, -0.090m, 0.075m);
            Add("Components", "Components", "Grouped blocks and parts", -0.010m, -0.040m, -0.075m, 0.065m);
            Add("Plantables", "Plantables", "Grouped blocks and parts", -0.010m, -0.040m, -0.080m, 0.075m);
            Add("QuestSpecial", "Quest / Special", "Grouped blocks and parts", -0.010m, -0.045m, -0.090m, 0.075m);
            Add("OtherParts", "Other Parts", "Grouped blocks and parts", -0.010m, -0.045m, -0.090m, 0.075m);
        }

        private void Add(string section, string name, string category, decimal x, decimal y,
            decimal z, decimal scale)
        {
            ProfileDef profile = new ProfileDef(section, name, category, x, y, z, scale);
            profiles.Add(profile);
            bySection.Add(section, profile);
            poses.Add(section, profile.Default.Clone());
        }

        private static Label MakeLabel(string text, float size, Color color)
        {
            return new Label { Text = text, AutoSize = true, Font = new Font("Segoe UI Semibold", size), ForeColor = color };
        }

        private Button MakeButton(string text)
        {
            Button button = new Button { Text = text, AutoSize = true, Height = 32, FlatStyle = FlatStyle.Flat,
                BackColor = Panel2, ForeColor = Color.White, Margin = new Padding(3, 0, 3, 0) };
            button.FlatAppearance.BorderColor = Color.FromArgb(88, 96, 104);
            button.FlatAppearance.MouseOverBackColor = Color.FromArgb(68, 74, 80);
            return button;
        }

        private void BuildUi()
        {
            TableLayoutPanel root = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 3,
                BackColor = Ink, Padding = new Padding(14) };
            root.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 285));
            root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            root.RowStyles.Add(new RowStyle(SizeType.Absolute, 112));
            root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            root.RowStyles.Add(new RowStyle(SizeType.Absolute, 42));
            Controls.Add(root);

            Panel header = new Panel { Dock = DockStyle.Fill, BackColor = Panel, Padding = new Padding(18, 12, 18, 10) };
            root.SetColumnSpan(header, 2); root.Controls.Add(header, 0, 0);
            Label title = MakeLabel("SM VR  /  HELD ITEM CALIBRATION", 15f, Accent);
            title.Location = new Point(16, 10); header.Controls.Add(title);
            activeItem.Text = "Waiting for Scrap Mechanic..."; activeItem.AutoSize = true;
            activeItem.Font = new Font("Segoe UI Semibold", 12f); activeItem.ForeColor = Color.White;
            activeItem.Location = new Point(17, 43); header.Controls.Add(activeItem);
            activeProfile.Text = "Hold an item in the right hand"; activeProfile.AutoSize = true;
            activeProfile.ForeColor = Muted; activeProfile.Location = new Point(18, 72); header.Controls.Add(activeProfile);
            followHeld.Text = "Follow currently held item"; followHeld.Checked = true; followHeld.AutoSize = true;
            followHeld.ForeColor = Color.White; followHeld.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            followHeld.Location = new Point(header.Width - 185, 18);
            header.Controls.Add(followHeld);
            header.Resize += delegate { followHeld.Left = header.ClientSize.Width - followHeld.Width - 18; };

            Panel navigation = new Panel { Dock = DockStyle.Fill, BackColor = Panel, Padding = new Padding(8, 10, 8, 10),
                Margin = new Padding(0, 10, 10, 8) };
            root.Controls.Add(navigation, 0, 1);
            profileTree.Dock = DockStyle.Fill; profileTree.BackColor = Panel; profileTree.ForeColor = Color.White;
            profileTree.BorderStyle = BorderStyle.None; profileTree.FullRowSelect = true; profileTree.HideSelection = false;
            profileTree.Font = new Font("Segoe UI", 9.5f); profileTree.ItemHeight = 25;
            profileTree.AfterSelect += delegate(object sender, TreeViewEventArgs e) {
                ProfileDef profile = e.Node.Tag as ProfileDef;
                if (profile != null) SelectProfile(profile.Section, true);
            };
            navigation.Controls.Add(profileTree);
            Dictionary<string, TreeNode> categories = new Dictionary<string, TreeNode>();
            foreach (ProfileDef profile in profiles)
            {
                TreeNode parent;
                if (!categories.TryGetValue(profile.Category, out parent))
                {
                    parent = new TreeNode(profile.Category); parent.ForeColor = Accent;
                    categories.Add(profile.Category, parent); profileTree.Nodes.Add(parent);
                }
                TreeNode node = new TreeNode(profile.Name); node.Tag = profile;
                parent.Nodes.Add(node); nodes.Add(profile.Section, node);
            }
            profileTree.ExpandAll();

            Panel editor = new Panel { Dock = DockStyle.Fill, BackColor = Panel, Padding = new Padding(22, 14, 22, 12),
                Margin = new Padding(0, 10, 0, 8) };
            root.Controls.Add(editor, 1, 1);
            TableLayoutPanel edit = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 10 };
            edit.RowStyles.Add(new RowStyle(SizeType.Absolute, 56));
            for (int i = 1; i <= 7; ++i) edit.RowStyles.Add(new RowStyle(SizeType.Percent, 14.285f));
            edit.RowStyles.Add(new RowStyle(SizeType.Absolute, 48));
            edit.RowStyles.Add(new RowStyle(SizeType.Absolute, 32));
            editor.Controls.Add(edit);
            editorTitle.Text = "Hammer"; editorTitle.AutoSize = true; editorTitle.Font = new Font("Segoe UI Semibold", 16f);
            editorTitle.ForeColor = Color.White;
            Label hint = new Label { Text = "Changes apply in VR while the game is running.", AutoSize = true,
                ForeColor = Muted, Location = new Point(1, 30) };
            Panel titlePanel = new Panel { Dock = DockStyle.Fill }; titlePanel.Controls.Add(editorTitle); titlePanel.Controls.Add(hint);
            edit.Controls.Add(titlePanel, 0, 0);

            AddPoseRow(edit, 1, "PositionX", "LEFT / RIGHT  (X)", -0.500m, 0.500m, 0.001m, 4, 1000m);
            AddPoseRow(edit, 2, "PositionY", "DOWN / UP  (Y)", -0.500m, 0.500m, 0.001m, 4, 1000m);
            AddPoseRow(edit, 3, "PositionZ", "BACK / FORWARD  (Z)", -0.500m, 0.500m, 0.001m, 4, 1000m);
            AddPoseRow(edit, 4, "PitchDegrees", "PITCH  (X ROTATION)", -180m, 180m, 0.25m, 2, 10m);
            AddPoseRow(edit, 5, "YawDegrees", "YAW  (Y ROTATION)", -180m, 180m, 0.25m, 2, 10m);
            AddPoseRow(edit, 6, "RollDegrees", "ROLL  (Z ROTATION)", -180m, 180m, 0.25m, 2, 10m);
            AddPoseRow(edit, 7, "Scale", "SCALE", 0.005m, 0.500m, 0.001m, 4, 1000m);

            FlowLayoutPanel buttons = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.LeftToRight,
                WrapContents = false, Padding = new Padding(0, 8, 0, 0) };
            Button reset = MakeButton("Reset this profile"); reset.Click += delegate { ResetSelected(); };
            Button copy = MakeButton("Copy pose"); copy.Click += delegate { clipboardPose = poses[selected.Section].Clone(); status.Text = "Pose copied."; };
            Button paste = MakeButton("Paste pose"); paste.Click += delegate { if (clipboardPose != null) { poses[selected.Section] = clipboardPose.Clone(); LoadSelectedControls(); QueueSave(); } };
            Button reload = MakeButton("Reload file"); reload.Click += delegate { LoadConfiguration(); LoadSelectedControls(); };
            Button folder = MakeButton("Open file"); folder.Click += delegate { Process.Start("explorer.exe", "/select,\"" + configPath + "\""); };
            CheckBox top = new CheckBox { Text = "Always on top", AutoSize = true, ForeColor = Color.White,
                Padding = new Padding(12, 7, 0, 0) };
            top.CheckedChanged += delegate { TopMost = top.Checked; };
            buttons.Controls.Add(reset); buttons.Controls.Add(copy); buttons.Controls.Add(paste);
            buttons.Controls.Add(reload); buttons.Controls.Add(folder); buttons.Controls.Add(top);
            edit.Controls.Add(buttons, 0, 8);
            Label gunNote = new Label { Dock = DockStyle.Fill, Text = "Gun muzzle position and firing direction follow the tuned gun pose.",
                ForeColor = Muted, TextAlign = ContentAlignment.MiddleLeft };
            edit.Controls.Add(gunNote, 0, 9);

            status.Dock = DockStyle.Fill; status.ForeColor = Muted; status.TextAlign = ContentAlignment.MiddleLeft;
            status.Text = configPath; root.SetColumnSpan(status, 2); root.Controls.Add(status, 0, 2);
        }

        private void AddPoseRow(TableLayoutPanel parent, int rowIndex, string key, string label,
            decimal min, decimal max, decimal increment, int decimals, decimal sliderFactor)
        {
            TableLayoutPanel row = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 3, Margin = new Padding(0, 2, 0, 2) };
            row.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 190));
            row.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 125));
            row.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            Label name = new Label { Text = label, Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft,
                ForeColor = key == "Scale" ? Accent : Color.White, Font = new Font("Segoe UI Semibold", 9f) };
            NumericUpDown number = new NumericUpDown { DecimalPlaces = decimals, Minimum = min, Maximum = max,
                Increment = increment, Dock = DockStyle.Fill, BackColor = Panel2, ForeColor = Color.White,
                BorderStyle = BorderStyle.FixedSingle, ThousandsSeparator = false };
            TrackBar slider = new TrackBar { Dock = DockStyle.Fill, TickStyle = TickStyle.None,
                Minimum = Decimal.ToInt32(min * sliderFactor), Maximum = Decimal.ToInt32(max * sliderFactor),
                SmallChange = Math.Max(1, Decimal.ToInt32(increment * sliderFactor)), LargeChange = Math.Max(2, Decimal.ToInt32(increment * sliderFactor * 10)) };
            PoseRow poseRow = new PoseRow { Key = key, Number = number, Slider = slider, SliderFactor = sliderFactor };
            rows.Add(key, poseRow);
            number.ValueChanged += delegate {
                if (loading || selected == null) return;
                int sliderValue = Decimal.ToInt32(number.Value * sliderFactor);
                slider.Value = Math.Max(slider.Minimum, Math.Min(slider.Maximum, sliderValue));
                SetPoseValue(poses[selected.Section], key, number.Value); QueueSave();
            };
            slider.Scroll += delegate {
                if (loading) return;
                decimal value = slider.Value / sliderFactor;
                value = Math.Max(number.Minimum, Math.Min(number.Maximum, value));
                number.Value = value;
            };
            row.Controls.Add(name, 0, 0); row.Controls.Add(number, 1, 0); row.Controls.Add(slider, 2, 0);
            parent.Controls.Add(row, 0, rowIndex);
        }

        private void SelectProfile(string section, bool manual)
        {
            ProfileDef profile;
            if (!bySection.TryGetValue(section, out profile)) return;
            selected = profile; editorTitle.Text = profile.Name;
            if (nodes.ContainsKey(section) && profileTree.SelectedNode != nodes[section])
                profileTree.SelectedNode = nodes[section];
            LoadSelectedControls();
            if (manual) status.Text = "Editing " + profile.Name + ".";
        }

        private void LoadSelectedControls()
        {
            if (selected == null) return;
            loading = true;
            Pose pose = poses[selected.Section];
            SetControl("PositionX", pose.X); SetControl("PositionY", pose.Y); SetControl("PositionZ", pose.Z);
            SetControl("PitchDegrees", pose.Pitch); SetControl("YawDegrees", pose.Yaw); SetControl("RollDegrees", pose.Roll);
            SetControl("Scale", pose.Scale);
            loading = false;
        }

        private void SetControl(string key, decimal value)
        {
            PoseRow row = rows[key];
            value = Math.Max(row.Number.Minimum, Math.Min(row.Number.Maximum, value));
            row.Number.Value = value;
            int sliderValue = Decimal.ToInt32(value * row.SliderFactor);
            row.Slider.Value = Math.Max(row.Slider.Minimum, Math.Min(row.Slider.Maximum, sliderValue));
        }

        private static decimal GetPoseValue(Pose pose, string key)
        {
            if (key == "PositionX") return pose.X; if (key == "PositionY") return pose.Y;
            if (key == "PositionZ") return pose.Z; if (key == "PitchDegrees") return pose.Pitch;
            if (key == "YawDegrees") return pose.Yaw; if (key == "RollDegrees") return pose.Roll;
            return pose.Scale;
        }

        private static void SetPoseValue(Pose pose, string key, decimal value)
        {
            if (key == "PositionX") pose.X = value; else if (key == "PositionY") pose.Y = value;
            else if (key == "PositionZ") pose.Z = value; else if (key == "PitchDegrees") pose.Pitch = value;
            else if (key == "YawDegrees") pose.Yaw = value; else if (key == "RollDegrees") pose.Roll = value;
            else pose.Scale = value;
        }

        private Dictionary<string, string> ParseIni()
        {
            Dictionary<string, string> values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            if (!File.Exists(configPath)) return values;
            string section = "";
            foreach (string raw in File.ReadAllLines(configPath))
            {
                string line = raw.Trim();
                if (line.StartsWith("[") && line.EndsWith("]")) section = line.Substring(1, line.Length - 2);
                else
                {
                    int equals = line.IndexOf('=');
                    if (equals > 0) values[section + "." + line.Substring(0, equals).Trim()] = line.Substring(equals + 1).Trim();
                }
            }
            return values;
        }

        private void LoadConfiguration()
        {
            loading = true;
            try
            {
                Dictionary<string, string> values = ParseIni();
                foreach (ProfileDef profile in profiles)
                {
                    Pose pose = profile.Default.Clone();
                    Read(values, profile.Section, "PositionX", ref pose.X);
                    Read(values, profile.Section, "PositionY", ref pose.Y);
                    Read(values, profile.Section, "PositionZ", ref pose.Z);
                    Read(values, profile.Section, "PitchDegrees", ref pose.Pitch);
                    Read(values, profile.Section, "YawDegrees", ref pose.Yaw);
                    Read(values, profile.Section, "RollDegrees", ref pose.Roll);
                    Read(values, profile.Section, "Scale", ref pose.Scale);
                    poses[profile.Section] = pose;
                }
                status.Text = "Loaded live calibration: " + configPath;
            }
            catch (Exception ex) { status.Text = "Load failed: " + ex.Message; }
            finally { loading = false; }
        }

        private static void Read(Dictionary<string, string> values, string section, string key, ref decimal value)
        {
            string text; decimal parsed;
            if (values.TryGetValue(section + "." + key, out text) &&
                Decimal.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out parsed)) value = parsed;
        }

        private void QueueSave()
        {
            saveTimer.Stop(); saveTimer.Start();
            status.Text = "Applying live update...";
        }

        private void SaveConfiguration()
        {
            try
            {
                StringBuilder text = new StringBuilder();
                text.AppendLine("; Scrap Mechanic Native VR live held-item calibration.");
                text.AppendLine("; Profiles are shared by the item groups shown in the helper.");
                foreach (ProfileDef profile in profiles)
                {
                    Pose pose = poses[profile.Section];
                    text.AppendLine(); text.AppendLine("[" + profile.Section + "]");
                    text.AppendLine("PositionX=" + pose.X.ToString("F4", CultureInfo.InvariantCulture));
                    text.AppendLine("PositionY=" + pose.Y.ToString("F4", CultureInfo.InvariantCulture));
                    text.AppendLine("PositionZ=" + pose.Z.ToString("F4", CultureInfo.InvariantCulture));
                    text.AppendLine("PitchDegrees=" + pose.Pitch.ToString("F2", CultureInfo.InvariantCulture));
                    text.AppendLine("YawDegrees=" + pose.Yaw.ToString("F2", CultureInfo.InvariantCulture));
                    text.AppendLine("RollDegrees=" + pose.Roll.ToString("F2", CultureInfo.InvariantCulture));
                    text.AppendLine("Scale=" + pose.Scale.ToString("F4", CultureInfo.InvariantCulture));
                }
                string temporary = configPath + ".tmp";
                File.WriteAllText(temporary, text.ToString(), new UTF8Encoding(false));
                if (File.Exists(configPath)) File.Replace(temporary, configPath, null);
                else File.Move(temporary, configPath);
                status.Text = "Applied live at " + DateTime.Now.ToString("HH:mm:ss.fff") + " — " + selected.Name;
            }
            catch (Exception ex) { status.Text = "Save failed: " + ex.Message; }
        }

        private void ResetSelected()
        {
            if (selected == null) return;
            poses[selected.Section] = selected.Default.Clone(); LoadSelectedControls(); QueueSave();
        }

        private static string JsonString(string text, string key)
        {
            Match match = Regex.Match(text, "\\\"" + Regex.Escape(key) + "\\\"\\s*:\\s*\\\"((?:\\\\.|[^\\\"])*)\\\"");
            if (!match.Success) return "";
            return Regex.Unescape(match.Groups[1].Value);
        }

        private void PollHeldStatus()
        {
            try
            {
                if (!File.Exists(statusPath))
                {
                    activeItem.Text = "Waiting for Scrap Mechanic...";
                    activeProfile.Text = "The game writes the active item here when VR starts.";
                    return;
                }
                DateTime write = File.GetLastWriteTimeUtc(statusPath);
                if (write == statusWriteTime) return;
                statusWriteTime = write;
                string text = File.ReadAllText(statusPath);
                string item = JsonString(text, "item"); string section = JsonString(text, "section");
                string profile = JsonString(text, "profile"); string uuid = JsonString(text, "uuid");
                bool isActive = Regex.IsMatch(text, "\\\"active\\\"\\s*:\\s*true", RegexOptions.IgnoreCase);
                activeItem.Text = isActive ? "RIGHT HAND:  " + item : "RIGHT HAND:  empty";
                activeProfile.Text = isActive ? "PROFILE:  " + profile + (uuid.Length > 0 ? "    UUID: " + uuid : "") :
                    "Select an item in the hotbar to tune it.";
                if (isActive && followHeld.Checked && bySection.ContainsKey(section)) SelectProfile(section, false);
            }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
        }
    }

    internal static class Program
    {
        [STAThread]
        private static void Main()
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new CalibrationForm());
        }
    }
}
