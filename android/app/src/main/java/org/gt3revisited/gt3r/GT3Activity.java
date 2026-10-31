package org.gt3revisited.gt3r;

import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.util.Log;
import android.view.DisplayCutout;
import android.view.RoundedCorner;
import android.view.View;
import android.view.WindowInsets;

import org.libsdl.app.SDLActivity;

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
