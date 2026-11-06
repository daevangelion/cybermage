package com.jshield.cybermage;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;

/** One top-level SAF import; never overlays the installed data file by file. */
final class DataImport implements AutoCloseable {
    private final File data, stage, previous;
    private final byte[] buffer = new byte[1 << 16];

    private static void clean(File directory) throws IOException {
        if (!directory.exists()) return;
        File[] files = directory.listFiles();
        if (files == null) throw new IOException("cannot list " + directory);
        for (File file : files) {
            if (!file.isFile() || !file.delete()) throw new IOException("cannot remove " + file);
        }
        if (!directory.delete()) throw new IOException("cannot remove " + directory);
    }

    static void recover(File data) throws IOException {
        File previous = new File(data.getPath() + ".previous");
        if (!data.exists() && previous.exists() && !previous.renameTo(data))
            throw new IOException("cannot restore previous import");
        if (data.exists()) clean(previous);
    }

    DataImport(File destination) throws IOException {
        data = destination;
        stage = new File(data.getPath() + ".import");
        previous = new File(data.getPath() + ".previous");
        recover(data);
        clean(stage);
        if (!stage.mkdir()) throw new IOException("cannot create import folder");
    }

    void copy(String name, InputStream input) throws IOException {
        if (name == null || name.isEmpty() || name.contains("/") || name.startsWith("."))
            throw new IOException("invalid import filename");
        File part = new File(stage, "." + name + ".part");
        try {
            try (InputStream in = input; FileOutputStream out = new FileOutputStream(part)) {
                if (in == null) throw new IOException("cannot open " + name);
                for (int n; (n = in.read(buffer)) != -1;) {
                    if (n == 0) throw new IOException("empty read from " + name);
                    out.write(buffer, 0, n);
                }
                out.getFD().sync();
            }
            if (!part.renameTo(new File(stage, name))) throw new IOException("cannot store " + name);
        } finally {
            if (part.exists() && !part.delete()) throw new IOException("cannot remove " + part);
        }
    }

    void finish(String[] required) throws IOException {
        for (String name : required) {
            if (!new File(stage, name).isFile()) throw new IOException("folder lacks " + name);
        }
        boolean hadData = data.exists();
        if (hadData && !data.renameTo(previous)) throw new IOException("cannot preserve previous import");
        if (!stage.renameTo(data)) {
            if (hadData && !previous.renameTo(data)) throw new IOException("cannot restore previous import");
            throw new IOException("cannot install imported folder");
        }
        clean(previous);
    }

    @Override public void close() throws IOException { clean(stage); }
}
