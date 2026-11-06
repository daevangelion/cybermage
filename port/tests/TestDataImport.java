package com.jshield.cybermage;

import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;

public final class TestDataImport {
    private static void require(boolean value) { if (!value) throw new AssertionError(); }
    public static void main(String[] args) throws Exception {
        File root = Files.createTempDirectory("cybermage-import-").toFile();
        File data = new File(root, "data");
        require(data.mkdir());
        try (FileOutputStream out = new FileOutputStream(new File(data, "OLD"))) { out.write(7); }
        try (DataImport copy = new DataImport(data)) {
            copy.copy("FIRST", new ByteArrayInputStream(new byte[] {1}));
            try {
                copy.copy("BROKEN", new InputStream() {
                    private int calls;
                    public int read() throws IOException { if (calls++ == 0) return 1; throw new IOException("read failure"); }
                });
                throw new AssertionError("read error was swallowed");
            } catch (IOException expected) { }
            require(!new File(root, "data.import/.BROKEN.part").exists());
        }
        require(new File(data, "OLD").isFile());
        require(!new File(data, "FIRST").exists());
        require(!new File(root, "data.import").exists());
        try (DataImport copy = new DataImport(data)) {
            copy.copy("NEW", new ByteArrayInputStream(new byte[] {2, 3}));
            copy.finish(new String[] {"NEW"});
        }
        require(!new File(data, "OLD").exists());
        require(Files.readAllBytes(new File(data, "NEW").toPath())[1] == 3);
        require(!new File(root, "data.previous").exists());
        try (DataImport copy = new DataImport(data)) {
            copy.copy("OTHER", new ByteArrayInputStream(new byte[] {4}));
            try { copy.finish(new String[] {"MISSING"}); throw new AssertionError(); }
            catch (IOException expected) { }
        }
        require(new File(data, "NEW").isFile() && !new File(data, "OTHER").exists());
        require(data.renameTo(new File(root, "data.previous")));
        DataImport.recover(data);
        require(new File(data, "NEW").isFile());
        require(new File(data, "NEW").delete() && data.delete() && root.delete());
    }
}
