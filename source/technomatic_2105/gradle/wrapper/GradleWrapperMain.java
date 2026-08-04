package org.gradle.wrapper;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.RandomAccessFile;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.channels.FileChannel;
import java.nio.channels.FileLock;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Properties;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * Small self-contained Gradle bootstrapper for this source distribution.
 * It intentionally implements only the wrapper behavior required by this project.
 */
public final class GradleWrapperMain {
    private static final int BUFFER_SIZE = 65536;
    private static final int MAX_REDIRECTS = 10;

    private GradleWrapperMain() {
    }

    public static void main(String[] args) {
        try {
            File projectDir = projectDirectory();
            File propertiesFile = new File(projectDir, "gradle/wrapper/gradle-wrapper.properties");
            Properties properties = loadProperties(propertiesFile);
            String distributionUrl = required(properties, "distributionUrl");
            String expectedSha256 = properties.getProperty("distributionSha256Sum", "").trim().toLowerCase(Locale.US);
            int timeoutMillis = parsePositiveInt(properties.getProperty("networkTimeout"), 30000);

            File gradleHome = installDistribution(distributionUrl, expectedSha256, timeoutMillis);
            int exit = runGradle(projectDir, gradleHome, args);
            System.exit(exit);
        } catch (Throwable error) {
            System.err.println("Gradle bootstrap failed: " + safeMessage(error));
            error.printStackTrace(System.err);
            System.exit(1);
        }
    }

    private static File projectDirectory() throws IOException {
        String configured = System.getProperty("technomatic.wrapper.root", "").trim();
        File root = configured.isEmpty() ? new File(System.getProperty("user.dir")) : new File(configured);
        return root.getCanonicalFile();
    }

    private static Properties loadProperties(File file) throws IOException {
        if (!file.isFile()) throw new IOException("Missing " + file.getAbsolutePath());
        Properties properties = new Properties();
        InputStream input = new FileInputStream(file);
        try {
            properties.load(input);
        } finally {
            input.close();
        }
        return properties;
    }

    private static String required(Properties properties, String key) throws IOException {
        String value = properties.getProperty(key, "").trim();
        if (value.isEmpty()) throw new IOException("Missing wrapper property: " + key);
        return value;
    }

    private static int parsePositiveInt(String value, int fallback) {
        try {
            int parsed = Integer.parseInt(value == null ? "" : value.trim());
            return parsed > 0 ? parsed : fallback;
        } catch (NumberFormatException ignored) {
            return fallback;
        }
    }

    private static File installDistribution(String distributionUrl,
                                            String expectedSha256,
                                            int timeoutMillis) throws Exception {
        String archiveName = archiveName(distributionUrl);
        String distributionName = archiveName.substring(0, archiveName.length() - 4);
        if (distributionName.endsWith("-bin")) {
            distributionName = distributionName.substring(0, distributionName.length() - 4);
        } else if (distributionName.endsWith("-all")) {
            distributionName = distributionName.substring(0, distributionName.length() - 4);
        }

        File userHome = new File(System.getProperty("user.home"));
        File cacheDir = new File(new File(new File(userHome, ".gradle"), "wrapper/dists"),
                archiveName.substring(0, archiveName.length() - 4) + "/" + shortHash(distributionUrl));
        if (!cacheDir.isDirectory() && !cacheDir.mkdirs()) {
            throw new IOException("Cannot create Gradle wrapper cache: " + cacheDir);
        }

        File lockFile = new File(cacheDir, ".install.lock");
        RandomAccessFile randomAccess = new RandomAccessFile(lockFile, "rw");
        FileChannel channel = randomAccess.getChannel();
        FileLock lock = channel.lock();
        try {
            File gradleHome = new File(cacheDir, distributionName);
            File executable = gradleExecutable(gradleHome);
            if (executable.isFile()) {
                executable.setExecutable(true, false);
                return gradleHome;
            }

            File archive = new File(cacheDir, archiveName);
            if (!archive.isFile() || !checksumMatches(archive, expectedSha256)) {
                if (archive.exists() && !archive.delete()) {
                    throw new IOException("Cannot replace invalid Gradle distribution: " + archive);
                }
                File partial = new File(cacheDir, archiveName + ".part");
                if (partial.exists() && !partial.delete()) {
                    throw new IOException("Cannot remove partial Gradle download: " + partial);
                }
                System.out.println("Downloading " + distributionUrl);
                download(distributionUrl, partial, timeoutMillis);
                if (!checksumMatches(partial, expectedSha256)) {
                    partial.delete();
                    throw new IOException("Gradle distribution SHA-256 does not match gradle-wrapper.properties");
                }
                Files.move(partial.toPath(), archive.toPath(), StandardCopyOption.REPLACE_EXISTING);
            }

            deleteRecursively(gradleHome);
            unzip(archive, cacheDir);
            executable = gradleExecutable(gradleHome);
            if (!executable.isFile()) {
                throw new IOException("Gradle distribution did not contain " + executable.getAbsolutePath());
            }
            executable.setExecutable(true, false);
            return gradleHome;
        } finally {
            try {
                lock.release();
            } finally {
                channel.close();
                randomAccess.close();
            }
        }
    }

    private static String archiveName(String distributionUrl) throws IOException {
        String path = new URL(distributionUrl).getPath();
        String name = path.substring(path.lastIndexOf('/') + 1);
        if (!name.endsWith(".zip")) throw new IOException("Gradle distribution must be a ZIP URL");
        return name;
    }

    private static String shortHash(String value) throws Exception {
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        byte[] bytes = digest.digest(value.getBytes("UTF-8"));
        StringBuilder result = new StringBuilder();
        for (int i = 0; i < 10; ++i) result.append(String.format(Locale.US, "%02x", bytes[i] & 0xff));
        return result.toString();
    }

    private static boolean checksumMatches(File file, String expected) throws Exception {
        if (expected == null || expected.isEmpty()) return file.isFile();
        return expected.equals(sha256(file));
    }

    private static String sha256(File file) throws Exception {
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        InputStream input = new BufferedInputStream(new FileInputStream(file));
        try {
            byte[] buffer = new byte[BUFFER_SIZE];
            int read;
            while ((read = input.read(buffer)) >= 0) {
                if (read > 0) digest.update(buffer, 0, read);
            }
        } finally {
            input.close();
        }
        StringBuilder result = new StringBuilder();
        for (byte value : digest.digest()) result.append(String.format(Locale.US, "%02x", value & 0xff));
        return result.toString();
    }

    private static void download(String source, File target, int timeoutMillis) throws IOException {
        URL current = new URL(source);
        for (int redirects = 0; redirects <= MAX_REDIRECTS; ++redirects) {
            HttpURLConnection connection = (HttpURLConnection) current.openConnection();
            connection.setInstanceFollowRedirects(false);
            connection.setConnectTimeout(timeoutMillis);
            connection.setReadTimeout(timeoutMillis);
            connection.setRequestProperty("User-Agent", "Technomatic-2105-Gradle-Wrapper/1");
            int status = connection.getResponseCode();
            if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
                String location = connection.getHeaderField("Location");
                connection.disconnect();
                if (location == null || location.trim().isEmpty()) throw new IOException("Gradle download redirect has no Location header");
                current = new URL(current, location);
                continue;
            }
            if (status < 200 || status >= 300) {
                String message = connection.getResponseMessage();
                connection.disconnect();
                throw new IOException("Gradle download returned HTTP " + status + " " + message);
            }

            InputStream input = new BufferedInputStream(connection.getInputStream());
            OutputStream output = new BufferedOutputStream(new FileOutputStream(target));
            try {
                byte[] buffer = new byte[BUFFER_SIZE];
                int read;
                while ((read = input.read(buffer)) >= 0) {
                    if (read > 0) output.write(buffer, 0, read);
                }
            } finally {
                try {
                    output.close();
                } finally {
                    input.close();
                    connection.disconnect();
                }
            }
            return;
        }
        throw new IOException("Too many redirects while downloading Gradle");
    }

    private static void unzip(File archive, File destination) throws IOException {
        String destinationPath = destination.getCanonicalPath() + File.separator;
        ZipInputStream input = new ZipInputStream(new BufferedInputStream(new FileInputStream(archive)));
        try {
            ZipEntry entry;
            byte[] buffer = new byte[BUFFER_SIZE];
            while ((entry = input.getNextEntry()) != null) {
                File outputFile = new File(destination, entry.getName());
                String outputPath = outputFile.getCanonicalPath();
                if (!outputPath.startsWith(destinationPath)) {
                    throw new IOException("Unsafe path in Gradle distribution: " + entry.getName());
                }
                if (entry.isDirectory()) {
                    if (!outputFile.isDirectory() && !outputFile.mkdirs()) {
                        throw new IOException("Cannot create directory: " + outputFile);
                    }
                } else {
                    File parent = outputFile.getParentFile();
                    if (!parent.isDirectory() && !parent.mkdirs()) {
                        throw new IOException("Cannot create directory: " + parent);
                    }
                    OutputStream output = new BufferedOutputStream(new FileOutputStream(outputFile));
                    try {
                        int read;
                        while ((read = input.read(buffer)) >= 0) {
                            if (read > 0) output.write(buffer, 0, read);
                        }
                    } finally {
                        output.close();
                    }
                }
                input.closeEntry();
            }
        } finally {
            input.close();
        }
    }

    private static File gradleExecutable(File gradleHome) {
        boolean windows = System.getProperty("os.name", "").toLowerCase(Locale.US).contains("win");
        return new File(new File(gradleHome, "bin"), windows ? "gradle.bat" : "gradle");
    }

    private static int runGradle(File projectDir, File gradleHome, String[] args) throws Exception {
        boolean windows = System.getProperty("os.name", "").toLowerCase(Locale.US).contains("win");
        File executable = gradleExecutable(gradleHome);
        List<String> command = new ArrayList<String>();
        if (windows) {
            command.add("cmd.exe");
            command.add("/d");
            command.add("/c");
        }
        command.add(executable.getAbsolutePath());
        for (String arg : args) command.add(arg);
        ProcessBuilder builder = new ProcessBuilder(command);
        builder.directory(projectDir);
        builder.inheritIO();
        Process process = builder.start();
        return process.waitFor();
    }

    private static void deleteRecursively(File file) throws IOException {
        if (file == null || !file.exists()) return;
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children != null) {
                for (File child : children) deleteRecursively(child);
            }
        }
        if (!file.delete()) throw new IOException("Cannot delete " + file);
    }

    private static String safeMessage(Throwable error) {
        String message = error == null ? null : error.getMessage();
        if (message == null || message.trim().isEmpty()) return error == null ? "unknown error" : error.getClass().getName();
        return message;
    }
}
