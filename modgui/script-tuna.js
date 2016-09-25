function (event) {

    function notename (note) {
        switch (note) {
            case 0:  return "C";
            case 1:  return "C#";
            case 2:  return "D";
            case 3:  return "D#";
            case 4:  return "E";
            case 5:  return "F";
            case 6:  return "F#";
            case 7:  return "G";
            case 8:  return "G#";
            case 9:  return "A";
            case 10: return "A#";
            case 11: return "B";
            default: break;
        }
        return "?";
    }

    function set_target_freq (ds) {
        var freq;
        if (ds['mode'] > 0) {
            freq = ds['mode'];
        } else {
            var note = ds['note'] + (ds['octave'] + 1) * 12;
            freq = ds['tuning'] * Math.pow(2.0, (note - 69.0) / 12.0);
        }
        event.icon.find ('[mod-role=target]').text (freq.toFixed(2) + " Hz");
    }

    function cent_visualization (ds) {
        var dpy_state;
        var cent = ds['cent'];
        if (cent <= -50 || cent >= 50) {
            dpy_state = -5;
        }
        else if (cent < -20) {
            dpy_state = -4;
        }
        else if (cent < -10) {
            dpy_state = -3;
        }
        else if (cent < -5) {
            dpy_state = -2;
        }
        else if (cent < -2) {
            dpy_state = -1;
        }
        else if (cent <= 2) {
            dpy_state = 0;
        }
        else if (cent <= 5) {
            dpy_state = 1;
        }
        else if (cent <= 10) {
            dpy_state = 2;
        }
        else if (cent <= 20) {
            dpy_state = 3;
        }
        else {
            dpy_state = 4;
        }

        if (dpy_state == ds['dpy_state']) {
            return;
        }
        ds['dpy_state'] = dpy_state;

        var dpy = event.icon.find ('[mod-role=tuner-display]');
        var adj;
        switch (dpy_state) {
            case -4:
                adj = ">>>>>>>>";
                break;
            case -3:
                adj = ">>>>>>";
                break;
            case -2:
                adj = ">>>>";
                break;
            case -1:
                adj = ">>";
                break;
            case -0:
                adj = ">|<";
                break;
            case 1:
                adj = "<<";
                break;
            case 2:
                adj = "<<<<";
                break;
            case 3:
                adj = "<<<<<<";
                break;
            case 4:
                adj = "<<<<<<<<";
                break;
            default:
                adj = "---";
                break;
        }
        if (dpy_state == 0) {
            dpy.addClass ('good');
        } else {
            dpy.removeClass ('good');
        }
        event.icon.find ('[mod-role=adjust]').text (adj);
    }

    function handle_event (symbol, value) {
        var dpy = event.icon.find ('[mod-role=tuner-display]');
        var ds = dpy.data ('xModPorts');
        switch (symbol) {
            case 'mode':
            case 'tuning':
                ds[symbol] = value;
                dpy.data ('xModPorts', ds);
                set_target_freq (ds);
                break;
            case 'freq_out':
                if (value < 10) {
                    dpy.addClass ('nosignal');
                    event.icon.find ('[mod-role=freq]').text ("----- Hz");
                    event.icon.find ('[mod-role=adjust]').text ("---");
                } else {
                    dpy.removeClass ('nosignal');
                    event.icon.find ('[mod-role=freq]').text (value.toFixed(1) + " Hz");
                }
                break;
            case 'octave':
                ds[symbol] = value;
                dpy.data ('xModPorts', ds);
                event.icon.find ('[mod-role=octave]').text (value.toFixed(0) + "");
                set_target_freq (ds);
                break;
            case 'note':
                ds[symbol] = value;
                dpy.data ('xModPorts', ds);
                event.icon.find ('[mod-role=note]').text (notename (Math.trunc(value)));
                set_target_freq (ds);
                break;
            case 'cent':
                ds[symbol] = value;
                dpy.data ('xModPorts', ds);
                event.icon.find ('[mod-role=cent]').text (value.toFixed(2) + " ct");
                cent_visualization (ds);
                break;
            case 'rms':
                break;
            case 'accuracy':
                break;
            case 'strobetoui':
                break;
            default:
                break;
        }
    }

    if (event.type == 'start') {
        var dpy = event.icon.find ('[mod-role=tuner-display]');
        var ds = {};
        ds['octave'] = 3;
        ds['note'] = 8;
        ds['cent'] = 0;
        ds['mode'] = 0;
        ds['dpy_state'] = -10;
        ds['freq_out'] = 0;

        dpy.data ('xModPorts', ds);

        var ports = event.ports;
        for (var p in ports) {
            handle_event (ports[p].symbol, ports[p].value);
        }
    }
    else if (event.type == 'change') {
        handle_event (event.symbol, event.value);
    }
}
