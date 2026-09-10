/*
 * Midi2Wav -- offline MIDI -> WAV render using the JDK's built-in Gervill
 * synthesizer with an external SoundFont.  Fallback for when `fluidsynth`
 * is not installed.
 *
 *   javac tools/Midi2Wav.java -d tools/classes \
 *         --add-exports java.desktop/com.sun.media.sound=ALL-UNNAMED
 *   java  --add-exports java.desktop/com.sun.media.sound=ALL-UNNAMED \
 *         -cp tools/classes Midi2Wav <sf2> <in.mid> <out.wav> [rate] [tailSec]
 *
 * Output: mono 16-bit PCM WAV at <rate> (default 44100).
 */
import java.io.*;
import javax.sound.midi.*;
import javax.sound.sampled.*;
import com.sun.media.sound.AudioSynthesizer;
import com.sun.media.sound.SoftSynthesizer;

public class Midi2Wav {
    public static void main(String[] args) throws Exception {
        if (args.length < 3) {
            System.err.println("usage: Midi2Wav <sf2> <in.mid> <out.wav> [rate] [tailSec]");
            System.exit(2);
        }
        File sf2  = new File(args[0]);
        File mid  = new File(args[1]);
        File out  = new File(args[2]);
        float rate = args.length > 3 ? Float.parseFloat(args[3]) : 44100f;
        double tail = args.length > 4 ? Double.parseDouble(args[4]) : 1.5;

        Sequence seq = MidiSystem.getSequence(mid);
        Soundbank bank = MidiSystem.getSoundbank(sf2);

        AudioSynthesizer synth = new SoftSynthesizer();
        AudioFormat fmt = new AudioFormat(rate, 16, 1, true, false); // mono s16 LE
        AudioInputStream stream = synth.openStream(fmt, null);

        synth.unloadAllInstruments(synth.getDefaultSoundbank());
        if (!synth.loadAllInstruments(bank))
            System.err.println("warning: soundbank not fully loaded");

        // Feed the whole sequence to the synth's receiver with real timestamps.
        Receiver recv = synth.getReceiver();
        double durSec = sendSequence(seq, recv);

        long frames = (long) (rate * (durSec + tail));
        AudioInputStream fixed = new AudioInputStream(stream, fmt, frames);
        AudioSystem.write(fixed, AudioFileFormat.Type.WAVE, out);

        synth.close();
    }

    /** send every event, return sequence length in seconds */
    private static double sendSequence(Sequence seq, Receiver recv) {
        float divType = seq.getDivisionType();
        int res = seq.getResolution();
        // default 120 bpm
        double usPerTick = (divType == Sequence.PPQ)
                ? 500000.0 / res
                : 1_000_000.0 / (divTypeFps(divType) * res);
        long lastTick = 0;
        double lastUs = 0;
        double maxUs = 0;

        // merge all tracks by absolute tick
        for (Track tr : seq.getTracks()) {
            // events within a track are already tick-ordered; we send per track
            // using absolute timestamps so cross-track order does not matter
            long curTick = 0;
            double curUs = 0;
            for (int i = 0; i < tr.size(); i++) {
                MidiEvent ev = tr.get(i);
                curUs += (ev.getTick() - curTick) * usPerTick;
                curTick = ev.getTick();
                MidiMessage m = ev.getMessage();
                if (m instanceof MetaMessage) {
                    MetaMessage mm = (MetaMessage) m;
                    if (mm.getType() == 0x51 && divType == Sequence.PPQ) {
                        byte[] d = mm.getData();
                        int tempo = ((d[0] & 0xff) << 16) | ((d[1] & 0xff) << 8) | (d[2] & 0xff);
                        usPerTick = (double) tempo / res;
                    }
                    continue; // don't forward meta
                }
                recv.send(m, (long) curUs);
                if (curUs > maxUs) maxUs = curUs;
            }
        }
        return maxUs / 1_000_000.0;
    }

    private static double divTypeFps(float divType) {
        if (divType == Sequence.SMPTE_24) return 24;
        if (divType == Sequence.SMPTE_25) return 25;
        if (divType == Sequence.SMPTE_30) return 30;
        return 29.97; // SMPTE_30DROP
    }
}
