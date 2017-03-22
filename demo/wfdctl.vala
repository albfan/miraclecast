using Org.Freedesktop.NetworkManager;
using Org.Freedesktop.Miracle.Wifi;
using Org.Freedesktop.Miracle.Wfd;

const string BUS_NAME_NETWORK_MANAGER = "org.freedesktop.NetworkManager";
const string BUS_NAME_WIFID = "org.freedesktop.miracle.wifi";
const string BUS_NAME_DISPD = "org.freedesktop.miracle.wfd";
const string OBJ_PATH_DEVICE = "/org/freedesktop/NetworkManager/Devices";
const string OBJ_PATH_LINK = "/org/freedesktop/miracle/wifi/link";
const string OBJ_PATH_PEER = "/org/freedesktop/miracle/wifi/peer";
const string OBJ_PATH_SINK = "/org/freedesktop/miracle/wfd/sink";
const string OBJ_PATH_SESSION = "/org/freedesktop/miracle/wfd/session";
const string IFACE_DEVICE = "org.freedesktop.NetworkManager.Device";
const string IFACE_LINK = "org.freedesktop.miracle.wifi.Link";
const string IFACE_PEER = "org.freedesktop.miracle.wifi.Peer";
const string IFACE_SINK = "org.freedesktop.miracle.wfd.Sink";
const string IFACE_SESSION = "org.freedesktop.miracle.wfd.Session";

errordomain WfdCtlError
{
	NO_SUCH_NIC,
	TIMEOUT,
	MONITOR_GONE,
}

private void print(string format, ...)
{
	var argv = va_list();
	stderr.printf("%s: ", Environment.get_prgname());
	stderr.vprintf(format, argv);
	stderr.printf("\n");
}

// to deal with sd_bus_path_encode/decode()ed path
private string decode_path(string s)
{
	char c;
	StringBuilder d = new StringBuilder();
	for(var i = 0; i < s.length; i ++) {
		if(s.data[i] == (uint8) '_') {
			c = (char) s.substring(i + 1, 2).to_long(null, 16);
			i += 2;
		}
		else {
			c = (char) s.data[i];
		}
		d.append_c(c);
	}

	return d.str;
}

private class WfdCtl : GLib.Application
{
	static string opt_iface;
	static string opt_wfd_subelems;
	static string opt_peer_mac;
	static string opt_display;
	static string opt_authority;
	static int opt_monitor_num;
	static string opt_audio_device;

	protected signal void link_added(string index, Link link);
	protected signal void link_removed(string index, Link link);
	protected signal void peer_added(string label, Peer peer);
	protected signal void peer_removed(string label, Peer peer);
	protected signal void sink_added(string label, Sink sink);
	protected signal void sink_removed(string label, Sink sink);
	protected signal void session_added(string id, Session session);
	protected signal void session_removed(string id, Session session);

	DBusObjectManagerClient nm;
	DBusObjectManagerClient wifi;
	DBusObjectManagerClient wfd;

	HashTable<string, Device> devices;
	HashTable<string, Link> links;
	HashTable<string, Peer> peers;
	HashTable<string, Sink> sinks;
	HashTable<string, Session> sessions;

	string curr_sink_mac;
	Gdk.Display display;

	const GLib.OptionEntry[] option_entries = {
		{ "interface", 'i', 0, OptionArg.STRING, ref opt_iface, "name of wireless network interface", "WNIC name" },
		{ "wfd-subelems", 'w', 0, OptionArg.STRING, ref opt_wfd_subelems, "device infomation.  default: 000600111c4400c8", "device info subelems" },
		{ "peer-mac", 'p', 0, OptionArg.STRING, ref opt_peer_mac, "MAC address of target peer", "peer MAC" },
		{ "authority", 'x', 0, OptionArg.STRING, ref opt_authority, "authority to capture from display. default: XAUTHORITY environment variable", "display authority" },
		{ "display", 'd', 0, OptionArg.STRING, ref opt_display, "display name.  default: DISPLAY environment variable", "display name" },
		{ "monitor-num", 'm', 0, OptionArg.INT, ref opt_monitor_num, "monitor number.  default: -1, primary monitor", "monitor number" },
		{ "audio-device", 'a', 0, OptionArg.STRING, ref opt_audio_device, "pulseaudio device name", "audio device name" },
		{ null },
	};

	public WfdCtl()
	{
		Object(application_id: "org.freedesktop.miracle.WfdCtl",
						flags: ApplicationFlags.FLAGS_NONE);

		devices = new HashTable<string, Device>(str_hash, str_equal);
		links = new HashTable<string, Link>(str_hash, str_equal);
		peers = new HashTable<string, Peer>(str_hash, str_equal);
		sinks = new HashTable<string, Sink>(str_hash, str_equal);
		sessions = new HashTable<string, Session>(str_hash, str_equal);

		add_main_option_entries(option_entries);
	}

	private void add_object(string path) throws Error
	{
		int sep = path.last_index_of_char('/');
		string prefix = path.substring(0, sep);
		string key = path.substring(sep + 1);
		switch(prefix) {
			case OBJ_PATH_DEVICE:
				Device d = Bus.get_proxy_sync(BusType.SYSTEM,
								BUS_NAME_NETWORK_MANAGER,
								path);
				if(is_wnic(d.interface)) {
					devices.insert(d.interface, d);
				}
				break;
			case OBJ_PATH_LINK:
				Link l = Bus.get_proxy_sync(BusType.SYSTEM,
								BUS_NAME_WIFID,
								path);
				key = decode_path(key);
				links.insert(key, l);
				info("found wireless interface: %s\n", l.interface_name);
				link_added(key, l);
				break;
			case OBJ_PATH_PEER:
				key = decode_path(key);
				if(peers.contains(key)) {
					break;
				}
				Peer p = Bus.get_proxy_sync(BusType.SYSTEM,
								BUS_NAME_WIFID,
								path);
				peers.insert(key, p);
				info("peer added: %s\n", key);
				peer_added(key, p);
				break;
			case OBJ_PATH_SINK:
				Sink s = Bus.get_proxy_sync(BusType.SYSTEM,
								BUS_NAME_DISPD,
								path);
				key = decode_path(key);
				sinks.insert(key, s);
				info("sink added: %s", key);
				sink_added(key, s);
				break;
			case OBJ_PATH_SESSION:
				Session s = Bus.get_proxy_sync(BusType.SYSTEM,
								BUS_NAME_DISPD,
								path);
				key = decode_path(key);
				sessions.insert(key, s);
				info("session added: %s", key);
				session_added(key, s);
				break;
		}
	}

	private void remove_object(string path)
	{
		debug("object removed: %s", path);

		int sep = path.last_index_of_char('/');
		string prefix = path.substring(0, sep);
		string key = path.substring(sep + 1);
		switch(prefix) {
			case OBJ_PATH_DEVICE:
				break;
			case OBJ_PATH_LINK:
				break;
			case OBJ_PATH_PEER:
				break;
			case OBJ_PATH_SINK:
				key = decode_path(key);
				break;
			case OBJ_PATH_SESSION:
				key = decode_path(key);
				break;
		}
	}

	private void on_object_added(DBusObjectManager m, DBusObject o)
	{
		try {
			add_object(o.get_object_path());
		}
		catch(Error e) {
			print("failed to fetch information from DBus for object: %s",
							o.get_object_path());
		}
	}

	private void on_object_removed(DBusObjectManager m, DBusObject o)
	{
		remove_object(o.get_object_path());
	}

	private void fetch_info_from_dbus() throws Error
	{
		wifi = new DBusObjectManagerClient.for_bus_sync(
						BusType.SYSTEM,
						DBusObjectManagerClientFlags.NONE,
						BUS_NAME_WIFID,
						"/org/freedesktop/miracle/wifi",
						null,
						null);
		wifi.object_added.connect(on_object_added);
		wifi.object_removed.connect(on_object_removed);

		nm = new DBusObjectManagerClient.for_bus_sync(
						BusType.SYSTEM,
						DBusObjectManagerClientFlags.NONE,
						BUS_NAME_NETWORK_MANAGER,
						"/org/freedesktop",
						null,
						null);
		nm.object_added.connect(on_object_added);
		nm.object_removed.connect(on_object_removed);

		wfd = new DBusObjectManagerClient.for_bus_sync(
						BusType.SYSTEM,
						DBusObjectManagerClientFlags.NONE,
						BUS_NAME_DISPD,
						"/org/freedesktop/miracle/wfd",
						null,
						null);
		wfd.object_added.connect(on_object_added);
		wfd.object_removed.connect(on_object_removed);

		foreach(var o in wifi.get_objects()) {
			add_object(o.get_object_path());
		}

		foreach(var o in nm.get_objects()) {
			add_object(o.get_object_path());
		}

		foreach(var o in wfd.get_objects()) {
			add_object(o.get_object_path());
		}
	}

	private async void start_p2p_scan() throws Error
	{
		Device d = find_device_by_name(opt_iface);
		if(null == d) {
			throw new WfdCtlError.NO_SUCH_NIC("no such wireless adapter: %s",
							opt_iface);
		}

		if(d.managed) {
			info("tell NetworkManager do not touch %s anymore", opt_iface);

			d.managed = false;
			yield wait_prop_changed(d as DBusProxy, "Managed");
		}

		Link l = find_link_by_name(opt_iface);
		if(null == l) {
			throw new WfdCtlError.NO_SUCH_NIC("no such wireless adapter: %s",
							opt_iface);
		}

		if(!l.managed) {
			info("let wifid manage %s", opt_iface);

			l.manage();
			yield wait_prop_changed(l as DBusProxy, "Managed");
		}

		if(l.wfd_subelements != opt_wfd_subelems) {
			info("update wfd_subelems to broadcast what kind of device we are");

			l.wfd_subelements = opt_wfd_subelems;
			yield wait_prop_changed(l as DBusProxy, "WfdSubelements");
		}

		if(!l.p2_p_scanning) {
			info("start P2P scanning...");
			l.p2_p_scanning = true;
			yield wait_prop_changed(l as DBusProxy, "P2PScanning");
		}

		print("wait for peer '%s'...", opt_peer_mac);
	}

	private async void wait_for_target_sink()
	{
		if(null != find_sink_by_mac(opt_peer_mac)) {
			return;
		}

		ulong id = sink_added.connect((l, s) => {
			if(null != find_sink_by_mac(opt_peer_mac)) {
				wait_for_target_sink.callback();
			}
		});

		yield;

		disconnect(id);
	}

	private async void form_p2p_group() throws Error
	{
		if(null != curr_sink_mac) {
			print("already hang out with sink: %s", curr_sink_mac);
			return;
		}

		Sink s = find_sink_by_mac(opt_peer_mac);
		curr_sink_mac = opt_peer_mac;

		string l = s.peer;
		l = decode_path(l.substring(l.last_index_of_char('/') + 1));
		Peer p = peers.lookup(l);

		info("forming P2P group with %s (%s)...", p.p2_p_mac, p.friendly_name);

		p.connect("auto", "");
		yield wait_prop_changed(p as DBusProxy, "Connected", 20);

		info("P2P group formed");
	}

	private async void establish_session() throws Error
	{
		weak Gdk.Monitor m = (-1 == opt_monitor_num)
						? display.get_primary_monitor()
						: display.get_monitor(opt_monitor_num);
		if(null == m) {
			throw new WfdCtlError.MONITOR_GONE("monitor %d gone",
							opt_monitor_num);
		}

		info("establishing display session...");

		Sink sink = find_sink_by_mac(opt_peer_mac);
		sink.start_session(opt_authority,
						@"x://$(opt_display)",
						m.geometry.x,
						m.geometry.y,
						m.geometry.width,
						m.geometry.height,
						null == opt_audio_device ? "" : opt_audio_device);
	}

	private async void start_wireless_display() throws Error
	{
		fetch_info_from_dbus();
		yield start_p2p_scan();
		yield wait_for_target_sink();
		yield form_p2p_group();
		yield establish_session();
	}

	private bool check_options()
	{
		if(null == opt_peer_mac) {
			print("please specify a peer MAC with -p option");
			return false;
		}

		if(null == opt_display) {
			opt_display = Environment.get_variable("DISPLAY");
			if(null == opt_display) {
				print("no video source found. play specify one by DISPLAY " +
								"environment or -d option");
				return false;
			}
			else {
				print("no display name specified by -d, " +
								"use DISPLAY environment variable instead");
			}
		}

		if(null == opt_authority) {
			opt_authority = Environment.get_variable("XAUTHORITY");
			if(null == opt_authority) {
				print("no display authority found. play specify one by XAUTHORITY " +
								"environment or -x option");
				return false;
			}
			else {
				print("no display authority specified by -x, " +
								"use XAUTHORITY environment variable instead");
			}
		}

		if(null == opt_iface) {
			opt_iface = "wlan0";
			print("no wireless adapter specified by -i, use '%s' instead",
							opt_iface);
		}

		if(null == opt_wfd_subelems) {
			opt_wfd_subelems = "000600111c4400c8";
			print("no wfd_subelems specified by -w, use '%s' instead",
							opt_wfd_subelems);
		}

		display = Gdk.Display.open(opt_display);
		if(null == display) {
			print("invalid display option: %s", opt_display);
			return false;
		}

		if(-1 != opt_monitor_num &&
						null == display.get_monitor(opt_monitor_num)) {
			print("invalid screen number option: %d", opt_monitor_num);
			return false;
		}

		return true;
	}

	protected override void activate()
	{
		if(!check_options()) {
			return;
		}

		hold();

		start_wireless_display.begin((o, r) => {
			try {
				start_wireless_display.end(r);
			}
			catch(Error e) {
				print("failed to cast to wireless display: %s", e.message);
				release();
			}
		});
	}

	private unowned Device? find_device_by_name(string nic_name)
	{
		foreach(var d in devices.get_values()) {
			if(nic_name == d.interface) {
				return d;
			}
		}

		return null;
	}

	private unowned Link? find_link_by_name(string nic_name)
	{
		foreach(var l in links.get_values()) {
			if(nic_name == l.interface_name) {
				return l;
			}
		}

		return null;
	}

	private unowned Sink? find_sink_by_mac(string m)
	{
		foreach(var l in sinks.get_keys()) {
			if(l.has_prefix(m)) {
				return sinks.lookup(l);
			}
		}

		return null;
	}

	private bool is_wnic(string nic_name)
	{
		return find_link_by_name(nic_name) != null;
	}

	private async void wait_prop_changed(DBusProxy o,
					string name,
					uint timeout = 0) throws WfdCtlError
	{
		ulong prop_changed_id = o.g_properties_changed.connect((props) => {
			string k;
			Variant v;
			foreach(var prop in props) {
				prop.get("{sv}", out k, out v);
				if(k == name) {
					wait_prop_changed.callback();
					break;
				}
			}
		});

		bool timed_out = false;
		uint timeout_id = 0;
		if(0 < timeout) {
			timeout_id = Timeout.add_seconds(timeout,
							() => {
								timed_out = true;
								wait_prop_changed.callback();
								return false;
							});
		}

		yield;

		if(0 < timeout) {
			Source.remove(timeout_id);
		}
		o.disconnect(prop_changed_id);

		if(timed_out) {
			throw new WfdCtlError.TIMEOUT("timeout to wait for property %s change",
							name);
		}
	}
}

int main(string[]? argv)
{
	Gdk.init(ref argv);
	Intl.setlocale();
	Environment.set_prgname(Path.get_basename(argv[0]));

	Application app = new WfdCtl();
	app.set_default();

	try {
		app.register();
	}
	catch(Error e) {
		print("failed to startup: %s", e.message);
		return 1;
	}

	return app.run(argv);
}
