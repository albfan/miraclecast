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

errordomain WfdCtlError
{
	NO_SUCH_NIC,
}

DBusObjectManagerClient nm;
DBusObjectManagerClient wifi;
DBusObjectManagerClient wfd;

HashTable<string, Device> devices;
HashTable<string, Link> links;
HashTable<string, Peer> peers;
HashTable<string, Sink> sinks;
HashTable<string, Session> sessions;

string curr_sink_id;
int retry_count = 0;

string opt_iface;
string opt_wfd_subelems;
string opt_peer_mac;

const GLib.OptionEntry[] option_entries = {
	{ "interface", 'i', 0, OptionArg.STRING, ref opt_iface, "name of wireless network interface", "WNIC name" },
	{ "wfd-subelems", 'w', 0, OptionArg.STRING, ref opt_wfd_subelems, "device infomation.  default: 000600111c4400c8", "device info subelems" },
	{ "peer-mac", 'p', 0, OptionArg.STRING, ref opt_peer_mac, "MAC address of target peer", "peer MAC" },
	{ null },
};

void print(string format, ...)
{
	var argv = va_list();
	stderr.printf("%s: ", Environment.get_prgname());
	stderr.vprintf(format, argv);
	stderr.printf("\n");
}

unowned Device? find_device_by_name(string nic_name)
{
	foreach(var d in devices.get_values()) {
		if(nic_name == d.interface) {
			return d;
		}
	}

	return null;
}

unowned Link? find_link_by_name(string nic_name)
{
	foreach(var l in links.get_values()) {
		if(nic_name == l.interface_name) {
			return l;
		}
	}

	return null;
}

unowned Sink? find_sink_by_label(string id)
{
	return sinks.lookup(id);
}

bool is_wnic(string nic_name)
{
	return find_link_by_name(nic_name) != null;
}

// to deal with sd_bus_path_encode/decode()ed path
string decode_path(string s)
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

private async void wait_prop_changed(DBusProxy o, string name)
{
	ulong id = o.g_properties_changed.connect((props) => {
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

	yield;

	o.disconnect(id);
}

async void add_object(DBusObject o) throws Error
{
	unowned string path = o.get_object_path();
	int sep = path.last_index_of_char('/');
	string prefix = path.substring(0, sep);
	string key = path.substring(sep + 1);
	switch(prefix) {
		case OBJ_PATH_DEVICE:
			Device dev = yield Bus.get_proxy(BusType.SYSTEM,
							BUS_NAME_NETWORK_MANAGER,
							path);
			if(!is_wnic(dev.interface)) {
				break;
			}
			devices.insert(dev.interface, dev);
			break;
		case OBJ_PATH_LINK:
			Link link = yield Bus.get_proxy(BusType.SYSTEM,
							BUS_NAME_WIFID,
							path);
			links.insert(key, link);
			info("found wnic: %s\n", link.interface_name);
			break;
		case OBJ_PATH_PEER:
			key = decode_path(key);
			if(!peers.contains(key)) {
				Peer peer = yield Bus.get_proxy(BusType.SYSTEM,
								BUS_NAME_WIFID,
								path);
				peers.insert(key, peer);
				info("found peer: %s\n", key);
			}
			break;
		case OBJ_PATH_SINK:
			key = decode_path(key);
			Sink sink = yield Bus.get_proxy(BusType.SYSTEM,
							BUS_NAME_DISPD,
							path);
			sinks.insert(key, sink);
			info("found sink: %s", key);
			form_p2p_group.begin(key, sink);
			break;
		case OBJ_PATH_SESSION:
			break;
	}
}

void on_object_added(DBusObjectManager m, DBusObject o)
{
	try {
		add_object.begin(o);
	}
	catch(Error e) {
		warning("error occured while adding newly created DBus object: %s",
						e.message);
	}
}

void on_object_removed(DBusObjectManager m, DBusObject o)
{
	//try {
		//remove_object.begin(o);
	//}
	//catch(Error e) {
		//warning("error occured while removing newly created DBus object: %s",
						//e.message);
	//}
}

async void fetch_info_from_dbus() throws Error
{
	wifi = yield DBusObjectManagerClient.new_for_bus(
					BusType.SYSTEM,
					DBusObjectManagerClientFlags.NONE,
					BUS_NAME_WIFID,
					"/org/freedesktop/miracle/wifi",
					null,
					null);
	foreach(var o in wifi.get_objects()) {
		yield add_object(o);
	}
	wifi.object_added.connect(on_object_added);
	wifi.object_removed.connect(on_object_removed);

	nm = yield DBusObjectManagerClient.new_for_bus(
					BusType.SYSTEM,
					DBusObjectManagerClientFlags.NONE,
					BUS_NAME_NETWORK_MANAGER,
					"/org/freedesktop",
					null,
					null);
	foreach(var o in nm.get_objects()) {
		yield add_object(o);
	}
	nm.object_added.connect(on_object_added);
	nm.object_removed.connect(on_object_removed);

	wfd = yield DBusObjectManagerClient.new_for_bus(
					BusType.SYSTEM,
					DBusObjectManagerClientFlags.NONE,
					BUS_NAME_DISPD,
					"/org/freedesktop/miracle/wfd",
					null,
					null);
	foreach(var o in wfd.get_objects()) {
		yield add_object(o);
	}
	wfd.object_added.connect(on_object_added);
	wfd.object_removed.connect(on_object_removed);
}

async void initiate_session() throws Error
{
	unowned Sink sink = find_sink_by_label(curr_sink_id);

	unowned string xauth = Environment.get_variable("XAUTHORITY");
	if(null == xauth) {
		error("no environment variable XAUTHORITY specified");
	}
	unowned string display = Environment.get_variable("DISPLAY");
	if(null == display) {
		error("no environment variable DISPLAY specified");
	}
	info(@"establishing display session...");
	sink.start_session(xauth,
					@"x://$(display).0",
					0, 0, 1920, 1080,
					"alsa_output.pci-0000_00_1b.0.analog-stereo.monitor");
}

async void form_p2p_group(string id, Sink sink) throws Error
{
	if(null != curr_sink_id) {
		print("already hang out with sink: %s", curr_sink_id);
		return;
	}

	if(!id.has_prefix(opt_peer_mac)) {
		print("not the sink we are waiting for: %s", id);
		return;
	}

	curr_sink_id = id;

	Peer p = peers.lookup(id);
	if(null == p) {
		p = yield Bus.get_proxy(BusType.SYSTEM,
						BUS_NAME_WIFID,
						sink.peer);
		peers.insert(id, p);
	}

	info("forming P2P group with %s (%s)...", p.p2_p_mac, p.friendly_name);

	uint timeout_id = Timeout.add_seconds(20, () => {
		p.disconnect();
		form_p2p_group.callback();
		return false;
	});
	ulong prop_changed_id = (p as DBusProxy).g_properties_changed.connect((props) => {
		foreach(var prop in props) {
			string k;
			Variant v;
			prop.get("{sv}", out k, out v);
			if("Connected" == k) {
				form_p2p_group.callback();
				break;
			}
		}
	});

	p.connect("auto", "");
	yield;

	Source.remove(timeout_id);
	(p as DBusProxy).disconnect(prop_changed_id);

	if(!p.connected) {
		++ retry_count;
		if(3 == retry_count) {
			print("tried our best to form P2P group but with failure, bye");
		}

		print("failed to form P2P group with %s, try again", p.p2_p_mac);

		curr_sink_id = null;

		form_p2p_group.begin(id, sink);
		return;
	}

	yield initiate_session();
}

async void start_p2p_scan() throws Error
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
	}

	info("wait for peer '%s'...", opt_peer_mac);
}

async void start_wireless_display() throws Error
{
	yield fetch_info_from_dbus();
	yield start_p2p_scan();
}

void app_activate(Application app)
{
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

	if(null == opt_peer_mac) {
		print("no peer MAC specified by -p, bye");
		app.release();
		return;
	}

	start_wireless_display.begin((o, r) => {
		try {
			start_wireless_display.end(r);
		}
		catch(Error e) {
			print("failed to fetch device information from DBus");
			app.release();
		}
	});
}

void app_startup(Application app)
{
	app.hold();
}

int main(string[]? argv)
{
	Intl.setlocale();
	Environment.set_prgname(Path.get_basename(argv[0]));

	devices = new HashTable<string, Device>(str_hash, str_equal);
	links = new HashTable<string, Link>(str_hash, str_equal);
	peers = new HashTable<string, Peer>(str_hash, str_equal);
	sinks = new HashTable<string, Sink>(str_hash, str_equal);
	sessions = new HashTable<string, Session>(str_hash, str_equal);

	var options = new OptionContext("- WfdCtl");
	options.set_help_enabled(true);
	options.add_main_entries(option_entries, null);

	Application app = new Application("org.freedesktop.miracle.WfdCtl",
					ApplicationFlags.FLAGS_NONE);
	app.set_default();
	app.startup.connect(app_startup);
	app.activate.connect(app_activate);

	try {
		options.parse(ref argv);
		app.register();
	}
	catch(Error e) {
		print("failed to startup: %s", e.message);
		return 1;
	}

	return app.run(argv);
}
