package org.gt3revisited.gt3r;

import android.Manifest;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.os.storage.StorageManager;
import android.os.storage.StorageVolume;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.provider.Settings;
import android.util.Log;
import android.view.DisplayCutout;
import android.view.RoundedCorner;
import android.view.View;
import android.view.WindowInsets;

import org.libsdl.app.SDLActivity;

import java.io.File;

/** Hosts the native engine. SDL is statically linked into libmain.so. */
public class GT3Activity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "main" };
    }

    // The game's Quit (SDL's main returned): SDL finishes the activity but keeps the process, which
    // cannot run the game again (SDL exits it inside the next onCreate and relies on the system
    // restarting it). Leave nothing to restart: the task leaves Recents and the process ends
    // once the activity is gone (the game has saved and shut down by then).
    @Override
    public void finish() {
        if (mSDLMainFinished) finishAndRemoveTask();
        else super.finish();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (mSDLMainFinished) android.os.Process.killProcess(android.os.Process.myPid());
    }

    // The disc image comes from the system file picker as a content:// URI.
    // Keep read access across app restarts so the remembered disc (and the
    // recent-discs list) still opens next launch.
    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode == REQUEST_MUSIC_FOLDER) {
            Uri tree = resultCode == RESULT_OK && data != null ? data.getData() : null;
            String path = tree != null ? treePath(tree) : null;
            folderPick = tree == null ? "C" : path != null ? "P" + path : "E";
            return;
        }
        if (resultCode == RESULT_OK && data != null && data.getData() != null
                && (data.getFlags() & Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION) != 0) {
            Uri uri = data.getData();
            try {
                getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
            } catch (SecurityException e) {
                Log.w("GT3R", "Could not keep access to " + uri, e);
            }
        }
        super.onActivityResult(requestCode, resultCode, data);
    }

    // The music folder (src/platform/music_folder.h): the scan reads it by its path, so the
    // game needs the permission to read audio files outside its own storage, and a folder the
    // system's picker can name a path for.
    private static final int REQUEST_MUSIC_FOLDER = 0x6733;
    private static final int REQUEST_MUSIC_ACCESS = 0x6734;
    private static final String EXTERNAL_STORAGE = "com.android.externalstorage.documents";
    private volatile String folderPick; // the picker's answer for takeFolderPick

    private static String musicPermission() {
        return Build.VERSION.SDK_INT >= 33 ? Manifest.permission.READ_MEDIA_AUDIO : Manifest.permission.READ_EXTERNAL_STORAGE;
    }

    /** Called from native code: whether the game may read audio files outside its own storage. */
    public boolean hasMusicAccess() {
        return checkSelfPermission(musicPermission()) == PackageManager.PERMISSION_GRANTED;
    }

    /**
     * Called from native code: asks for the permission to read audio files when the game doesn't
     * have it, then shows the system's folder picker (at Music). The answer comes from
     * takeFolderPick.
     */
    public boolean pickMusicFolder() {
        folderPick = null;
        runOnUiThread(() -> {
            if (hasMusicAccess()) showFolderPicker();
            else requestPermissions(new String[] {musicPermission()}, REQUEST_MUSIC_ACCESS);
        });
        return true;
    }

    /**
     * Called from native code while the picker is open: null until it is answered, then "P" and
     * the folder's path, "C" (cancelled), "A" (reading audio files not allowed), "E" (a folder
     * with no path: another app's provider) or "F" (no picker on this device).
     */
    public String takeFolderPick() {
        String a = folderPick;
        if (a != null) folderPick = null;
        return a;
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        if (requestCode != REQUEST_MUSIC_ACCESS) {
            super.onRequestPermissionsResult(requestCode, permissions, grantResults);
            return;
        }
        if (grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED) showFolderPicker();
        else folderPick = "A";
    }

    private void showFolderPicker() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        if (Build.VERSION.SDK_INT >= 26)
            i.putExtra(DocumentsContract.EXTRA_INITIAL_URI, DocumentsContract.buildDocumentUri(EXTERNAL_STORAGE, "primary:Music"));
        try {
            startActivityForResult(i, REQUEST_MUSIC_FOLDER);
        } catch (ActivityNotFoundException | SecurityException e) {
            Log.i("GT3R", "No folder picker: " + e);
            folderPick = "F";
        }
    }

    // The path of a folder from the system's picker: the device's storage or a memory card
    // (document ids "primary:Music/Rock", "1234-ABCD:Music"), a Downloads folder ("raw:" ids),
    // or this app's own folder (GT3DocumentsProvider); null for other providers.
    private String treePath(Uri tree) {
        String id;
        try {
            id = DocumentsContract.getTreeDocumentId(tree);
        } catch (IllegalArgumentException e) {
            return null;
        }
        String authority = tree.getAuthority();
        if (EXTERNAL_STORAGE.equals(authority)) {
            int colon = id.indexOf(':');
            if (colon < 0) return null;
            String volume = id.substring(0, colon), rel = id.substring(colon + 1);
            File base = null;
            if (volume.equals("primary")) base = Environment.getExternalStorageDirectory();
            else if (volume.equals("home")) base = new File(Environment.getExternalStorageDirectory(), Environment.DIRECTORY_DOCUMENTS);
            else {
                if (Build.VERSION.SDK_INT >= 30) {
                    StorageManager sm = getSystemService(StorageManager.class);
                    for (StorageVolume v : sm.getStorageVolumes())
                        if (volume.equalsIgnoreCase(v.getUuid()) && v.getDirectory() != null) base = v.getDirectory();
                }
                if (base == null) base = new File("/storage/" + volume);
            }
            return rel.isEmpty() ? base.getPath() : new File(base, rel).getPath();
        }
        if (id.startsWith("raw:")) return id.substring(4);
        if (GT3DocumentsProvider.authority(this).equals(authority)) {
            String root = GT3DocumentsProvider.ROOT_ID;
            if (id.equals(root)) return getFilesDir().getPath();
            if (id.startsWith(root + "/")) return new File(getFilesDir(), id.substring(root.length() + 1)).getPath();
        }
        return null;
    }

    /** Called from native code: the game's page in Android's settings (its permissions). */
    public boolean openAppSettings() {
        Intent i = new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.fromParts("package", getPackageName(), null));
        try {
            startActivity(i);
            return true;
        } catch (ActivityNotFoundException | SecurityException e) {
            Log.i("GT3R", "No app settings: " + e);
            return false;
        }
    }

    /**
     * Called from native code (src/platform/display_insets.cpp): the display
     * cutout per edge and the largest rounded-corner radius, in pixels, as
     * {left, top, right, bottom, radius}; null when not known yet. Unlike
     * SDL's safe area this leaves out the system-gesture zones.
     */
    public int[] displayInsets() {
        if (Build.VERSION.SDK_INT < 28) return null;
        View decor = getWindow() != null ? getWindow().getDecorView() : null;
        WindowInsets insets = decor != null ? decor.getRootWindowInsets() : null;
        if (insets == null) return null;
        int[] out = new int[5];
        DisplayCutout cut = insets.getDisplayCutout();
        if (cut != null) {
            out[0] = cut.getSafeInsetLeft();
            out[1] = cut.getSafeInsetTop();
            out[2] = cut.getSafeInsetRight();
            out[3] = cut.getSafeInsetBottom();
        }
        if (Build.VERSION.SDK_INT >= 31) {
            int[] positions = {RoundedCorner.POSITION_TOP_LEFT, RoundedCorner.POSITION_TOP_RIGHT,
                               RoundedCorner.POSITION_BOTTOM_LEFT, RoundedCorner.POSITION_BOTTOM_RIGHT};
            for (int p : positions) {
                RoundedCorner c = insets.getRoundedCorner(p);
                if (c != null) out[4] = Math.max(out[4], c.getRadius());
            }
        }
        return out;
    }

    /**
     * Called from native code (src/platform/user_files.cpp): shows the user data folder in the
     * system Files app through GT3DocumentsProvider, at the folder `relPath` inside it ("" for
     * the folder itself) on Android 8 and later (the Files app finds it with findDocumentPath),
     * else at its GT3 Revisited entry. False when no app could open it.
     */
    public boolean openUserFolder(String relPath) {
        String authority = GT3DocumentsProvider.authority(this);
        String root = GT3DocumentsProvider.ROOT_ID;
        Intent folder = new Intent(Intent.ACTION_VIEW);
        folder.setDataAndType(DocumentsContract.buildDocumentUri(authority, relPath.isEmpty() ? root : root + "/" + relPath),
                              DocumentsContract.Document.MIME_TYPE_DIR);
        Intent top = new Intent(Intent.ACTION_VIEW);
        top.setDataAndType(DocumentsContract.buildRootUri(authority, root), DocumentsContract.Root.MIME_TYPE_ITEM);
        Intent[] tries = Build.VERSION.SDK_INT >= 26 ? new Intent[] {folder, top} : new Intent[] {top};
        for (Intent i : tries) {
            i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
            try {
                startActivity(i);
                return true;
            } catch (ActivityNotFoundException | SecurityException e) {
                Log.i("GT3R", "No app opens " + i.getData() + ": " + e);
            }
        }
        return false;
    }

    /**
     * Called from native code (src/platform/user_files.cpp): the name of a file picked in the
     * file dialog (a content:// URI, which itself often holds only a document number); null
     * when its provider does not say.
     */
    public String uriDisplayName(String uri) {
        try (Cursor c = getContentResolver().query(Uri.parse(uri), new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (c != null && c.moveToFirst() && !c.isNull(0)) return c.getString(0);
        } catch (RuntimeException e) {
            Log.w("GT3R", "No name for " + uri, e);
        }
        return null;
    }
}
