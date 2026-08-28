using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Text;
using System.Windows.Forms;

[assembly: System.Reflection.AssemblyTitle("Scrap Mechanic VR Clay Gun Calibrator")]
[assembly: System.Reflection.AssemblyDescription("Live Chapter 2 clay gun pose and animation calibration")]
[assembly: System.Reflection.AssemblyVersion("0.2.5.0")]
[assembly: System.Reflection.AssemblyFileVersion("0.2.5.0")]

namespace ScrapMechanicVRClayCalibration
{
    internal sealed class Setting
    {
        internal string Section, Key, Label, Help;
        internal decimal Default, Minimum, Maximum, Increment;
        internal int Decimals;
        internal NumericUpDown Control;
    }

    internal sealed class CalibrationForm : Form
    {
        private readonly string configPath = Path.Combine(
            AppDomain.CurrentDomain.BaseDirectory, "ScrapMechanicVR-ClayCalibration.ini");
        private readonly List<Setting> settings = new List<Setting>();
        private readonly Timer saveTimer = new Timer();
        private readonly Label status = new Label();
        private bool loading;

        internal CalibrationForm()
        {
            Text = "Scrap Mechanic VR - Clay Gun Live Calibrator";
            ClientSize = new Size(780, 760);
            MinimumSize = new Size(720, 620);
            StartPosition = FormStartPosition.CenterScreen;
            Font = new Font("Segoe UI", 9.0f);
            TopMost = true;

            BuildSettings();
            BuildUi();
            LoadConfiguration();

            saveTimer.Interval = 120;
            saveTimer.Tick += delegate { saveTimer.Stop(); SaveConfiguration(); };
        }

        private void BuildSettings()
        {
            Add("Tool", "PositionX", "Position X", "Controller-local metres.", -0.122m, -2m, 2m, 0.002m, 4);
            Add("Tool", "PositionY", "Position Y", "Controller-local metres.", -0.031m, -2m, 2m, 0.002m, 4);
            Add("Tool", "PositionZ", "Position Z", "Controller-local metres.", -0.172m, -2m, 2m, 0.002m, 4);
            Add("Tool", "PitchDegrees", "Pitch", "Rotation about controller X, degrees.", 0m, -180m, 180m, 0.5m, 2);
            Add("Tool", "YawDegrees", "Yaw", "Rotation about controller Y, degrees.", 0m, -180m, 180m, 0.5m, 2);
            Add("Tool", "RollDegrees", "Roll", "Rotation about controller Z, degrees.", 0m, -180m, 180m, 0.5m, 2);
            Add("Tool", "Scale", "Scale", "Native clay mesh scale.", 0.145m, 0.01m, 1m, 0.001m, 4);

            Add("Container", "PivotX", "Pivot X", "Mesh-local rotation pivot.", 0.040000m, -10m, 10m, 0.01m, 5);
            Add("Container", "PivotY", "Pivot Y", "Mesh-local rotation pivot.", 0.510000m, -10m, 10m, 0.01m, 5);
            Add("Container", "PivotZ", "Pivot Z", "Mesh-local rotation pivot.", 0.160580m, -10m, 10m, 0.01m, 5);
            Add("Container", "AxisX", "Axis X", "Arbitrary mesh-local axis; normalized in the renderer.", 0m, -1m, 1m, 0.05m, 3);
            Add("Container", "AxisY", "Axis Y", "Arbitrary mesh-local axis; normalized in the renderer.", 0m, -1m, 1m, 0.05m, 3);
            Add("Container", "AxisZ", "Axis Z", "Arbitrary mesh-local axis; normalized in the renderer.", 1m, -1m, 1m, 0.05m, 3);
            Add("Container", "SpeedMultiplier", "Speed", "Use -1 to reverse or 0 to freeze for phase tuning.", 1m, -5m, 5m, 0.05m, 3);
            Add("Container", "PhaseDegrees", "Manual phase", "Added rotation in degrees; easiest to inspect with speed 0.", 0m, -360m, 360m, 1m, 2);

            Add("Wheel", "PivotX", "Pivot X", "Mesh-local rotation pivot.", -0.010000m, -10m, 10m, 0.01m, 5);
            Add("Wheel", "PivotY", "Pivot Y", "Mesh-local rotation pivot.", 0.239770m, -10m, 10m, 0.01m, 5);
            Add("Wheel", "PivotZ", "Pivot Z", "Mesh-local rotation pivot.", 1.416530m, -10m, 10m, 0.01m, 5);
            Add("Wheel", "AxisX", "Axis X", "Arbitrary mesh-local axis; normalized in the renderer.", 1m, -1m, 1m, 0.05m, 3);
            Add("Wheel", "AxisY", "Axis Y", "Arbitrary mesh-local axis; normalized in the renderer.", 0m, -1m, 1m, 0.05m, 3);
            Add("Wheel", "AxisZ", "Axis Z", "Arbitrary mesh-local axis; normalized in the renderer.", 0m, -1m, 1m, 0.05m, 3);
            Add("Wheel", "SpeedMultiplier", "Speed", "Use -1 to reverse or 0 to freeze for phase tuning.", 1m, -5m, 5m, 0.05m, 3);
            Add("Wheel", "PhaseDegrees", "Manual phase", "Added rotation in degrees; easiest to inspect with speed 0.", 0m, -360m, 360m, 1m, 2);
        }

        private void Add(string section, string key, string label, string help,
            decimal value, decimal min, decimal max, decimal increment, int decimals)
        {
            settings.Add(new Setting { Section = section, Key = key, Label = label, Help = help,
                Default = value, Minimum = min, Maximum = max, Increment = increment, Decimals = decimals });
        }

        private void BuildUi()
        {
            TableLayoutPanel root = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1,
                RowCount = 4, Padding = new Padding(12) };
            root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            Controls.Add(root);

            Label help = new Label { AutoSize = true, MaximumSize = new Size(735, 0),
                Text = "Keep this window open beside Scrap Mechanic. Changes are written automatically and appear in VR within about 100 ms. Select the clay gun. For a moving part, set Speed to 0 and adjust Manual phase, pivot and axis; restore Speed to 1 (or -1) when aligned." };
            root.Controls.Add(help, 0, 0);

            FlowLayoutPanel groups = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoScroll = true,
                FlowDirection = FlowDirection.LeftToRight, WrapContents = true, Padding = new Padding(0, 10, 0, 0) };
            root.Controls.Add(groups, 0, 1);
            ToolTip tips = new ToolTip { AutoPopDelay = 15000, InitialDelay = 300, ReshowDelay = 100 };
            foreach (string section in new[] { "Tool", "Container", "Wheel" })
            {
                GroupBox box = new GroupBox { Text = section, Width = 235, Height = 520, Padding = new Padding(8) };
                TableLayoutPanel table = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, AutoScroll = true };
                table.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 52));
                table.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 48));
                box.Controls.Add(table);
                foreach (Setting setting in settings.FindAll(s => s.Section == section))
                {
                    Label label = new Label { Text = setting.Label, AutoSize = true, Anchor = AnchorStyles.Left };
                    NumericUpDown input = new NumericUpDown { DecimalPlaces = setting.Decimals,
                        Minimum = setting.Minimum, Maximum = setting.Maximum, Increment = setting.Increment,
                        Value = setting.Default, Width = 96, Anchor = AnchorStyles.Left | AnchorStyles.Right };
                    setting.Control = input;
                    input.ValueChanged += delegate { if (!loading) QueueSave(); };
                    tips.SetToolTip(label, setting.Help);
                    tips.SetToolTip(input, setting.Help);
                    table.Controls.Add(label);
                    table.Controls.Add(input);
                }
                groups.Controls.Add(box);
            }

            FlowLayoutPanel buttons = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = true };
            Button save = new Button { Text = "Apply now", AutoSize = true };
            save.Click += delegate { saveTimer.Stop(); SaveConfiguration(); };
            Button reload = new Button { Text = "Reload file", AutoSize = true };
            reload.Click += delegate { LoadConfiguration(); };
            Button reset = new Button { Text = "Reset defaults", AutoSize = true };
            reset.Click += delegate { ResetDefaults(); };
            Button folder = new Button { Text = "Open folder", AutoSize = true };
            folder.Click += delegate { Process.Start("explorer.exe", "/select,\"" + configPath + "\""); };
            CheckBox top = new CheckBox { Text = "Always on top", Checked = true, AutoSize = true, Padding = new Padding(12, 5, 0, 0) };
            top.CheckedChanged += delegate { TopMost = top.Checked; };
            buttons.Controls.Add(save); buttons.Controls.Add(reload); buttons.Controls.Add(reset);
            buttons.Controls.Add(folder); buttons.Controls.Add(top);
            root.Controls.Add(buttons, 0, 2);

            status.AutoSize = true;
            status.Text = configPath;
            root.Controls.Add(status, 0, 3);
        }

        private void QueueSave()
        {
            saveTimer.Stop();
            saveTimer.Start();
            status.Text = "Pending live update...";
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
                foreach (Setting setting in settings)
                {
                    decimal value = setting.Default;
                    string text;
                    decimal parsed;
                    if (values.TryGetValue(setting.Section + "." + setting.Key, out text) &&
                        Decimal.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out parsed))
                        value = Math.Max(setting.Minimum, Math.Min(setting.Maximum, parsed));
                    setting.Control.Value = value;
                }
                status.Text = "Loaded: " + configPath;
            }
            catch (Exception ex) { status.Text = "Load failed: " + ex.Message; }
            finally { loading = false; }
        }

        private void ResetDefaults()
        {
            loading = true;
            foreach (Setting setting in settings) setting.Control.Value = setting.Default;
            loading = false;
            SaveConfiguration();
        }

        private void SaveConfiguration()
        {
            try
            {
                StringBuilder text = new StringBuilder();
                text.AppendLine("; Live Scrap Mechanic VR Chapter 2 clay gun calibration.");
                foreach (string section in new[] { "Tool", "Container", "Wheel" })
                {
                    text.AppendLine("[" + section + "]");
                    foreach (Setting setting in settings.FindAll(s => s.Section == section))
                        text.AppendLine(setting.Key + "=" + setting.Control.Value.ToString("F" + setting.Decimals, CultureInfo.InvariantCulture));
                    text.AppendLine();
                }
                string temporary = configPath + ".tmp";
                File.WriteAllText(temporary, text.ToString(), new UTF8Encoding(false));
                if (File.Exists(configPath)) File.Replace(temporary, configPath, null);
                else File.Move(temporary, configPath);
                status.Text = "Applied live at " + DateTime.Now.ToString("HH:mm:ss.fff") + " — " + configPath;
            }
            catch (Exception ex) { status.Text = "Save failed: " + ex.Message; }
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
