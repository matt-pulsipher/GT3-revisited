package org.gt3revisited.gt3r;

import android.content.ContentResolver;
import android.content.Intent;
import android.content.UriPermission;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.provider.DocumentsContract.Document;
import android.util.Log;

import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.Iterator;
import java.util.Locale;
import java.util.Map;

/**
 * The files of a folder the player picked (ACTION_OPEN_DOCUMENT_TREE), through its documents
 * provider: src/platform/document_tree.h. Native code names them by paths, the folder's tree URI
 * then "/" and the names inside it; each name's document is found among its folder's children
 * (by its exact name, else ignoring case) and remembered. A remembered document is checked
 * before use: other apps (a sync app) change the folder too.
 */
final class DocumentTree {
    private static final String TAG = "GT3R";
    // Every file is plain bytes: a provider then keeps the name as given (with another type it
    // may add the type's extension).
    private static final String FILE_TYPE = "application/octet-stream";

    private final ContentResolver resolver;
    private final HashMap<String, String> ids = new HashMap<>(); // path -> document id

    DocumentTree(ContentResolver resolver) {
        this.resolver = resolver;
    }

    /** A path split into its tree and the names in it; null when it is not in a tree. */
    private static final class Path {
        final String tree; // the tree URI as given
        final Uri treeUri;
        final String[] names;

        Path(String tree, String[] names) {
            this.tree = tree;
            this.treeUri = Uri.parse(tree);
            this.names = names;
        }

        /** The path of the first `n` names. */
        String key(int n) {
            StringBuilder b = new StringBuilder(tree);
            for (int i = 0; i < n; i++) b.append('/').append(names[i]);
            return b.toString();
        }

        Path parent() {
            return new Path(tree, Arrays.copyOf(names, names.length - 1));
        }

        String name() {
            return names[names.length - 1];
        }
    }

    // As tree_end in document_tree.cpp.
    private static Path split(String path) {
        if (path == null || !path.startsWith("content://")) return null;
        int authEnd = path.indexOf('/', 10);
        if (authEnd < 0 || !path.startsWith("/tree/", authEnd)) return null;
        int id = authEnd + 6;
        int end = path.indexOf('/', id);
        if (end < 0) end = path.length();
        if (end == id || path.startsWith("/document/", end)) return null;
        ArrayList<String> names = new ArrayList<>();
        for (String n : path.substring(end).split("/"))
            if (!n.isEmpty()) names.add(n);
        return new Path(path.substring(0, end), names.toArray(new String[0]));
    }

    private Uri docUri(Path p, String id) {
        return DocumentsContract.buildDocumentUriUsingTree(p.treeUri, id);
    }

    /** One document's type, last change and size: {mime, modified (ms), size}; null when gone. */
    private String[] query(Path p, String id) {
        String[] cols = {Document.COLUMN_MIME_TYPE, Document.COLUMN_LAST_MODIFIED, Document.COLUMN_SIZE};
        try (Cursor c = resolver.query(docUri(p, id), cols, null, null, null)) {
            if (c == null || !c.moveToFirst()) return null;
            return new String[] {c.getString(0), c.isNull(1) ? "0" : Long.toString(c.getLong(1)),
                                 c.isNull(2) ? "0" : Long.toString(c.getLong(2))};
        } catch (RuntimeException e) {
            return null;
        }
    }

    /**
     * The children of the folder `dir` (document `id`), remembered under their paths: entries as
     * document_tree.cpp parses them ("F" or "D", modified ms, ':', size, ':', name), or null when
     * the folder cannot be read.
     */
    private ArrayList<String> children(Path dir, String id) {
        String[] cols = {Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME, Document.COLUMN_MIME_TYPE,
                         Document.COLUMN_LAST_MODIFIED, Document.COLUMN_SIZE};
        Uri uri = DocumentsContract.buildChildDocumentsUriUsingTree(dir.treeUri, id);
        ArrayList<String> out = new ArrayList<>();
        String prefix = dir.key(dir.names.length) + "/";
        try (Cursor c = resolver.query(uri, cols, null, null, null)) {
            if (c == null) return null;
            // Gone ones are forgotten; the others are found again below.
            for (Iterator<Map.Entry<String, String>> it = ids.entrySet().iterator(); it.hasNext();) {
                String k = it.next().getKey();
                if (k.startsWith(prefix) && k.indexOf('/', prefix.length()) < 0) it.remove();
            }
            while (c.moveToNext()) {
                String child = c.getString(0), name = c.getString(1), mime = c.getString(2);
                if (child == null || name == null || name.isEmpty() || name.indexOf('/') >= 0) continue;
                ids.put(prefix + name, child);
                out.add((Document.MIME_TYPE_DIR.equals(mime) ? "D" : "F") + (c.isNull(3) ? 0 : c.getLong(3)) + ":"
                        + (c.isNull(4) ? 0 : c.getLong(4)) + ":" + name);
            }
            return out;
        } catch (RuntimeException e) {
            Log.w(TAG, "Cannot list " + prefix + ": " + e);
            return null;
        }
    }

    /** The document id of the first `n` names of `p`, or null when one is missing. */
    private String find(Path p, int n) {
        if (n == 0) {
            try {
                return DocumentsContract.getTreeDocumentId(p.treeUri);
            } catch (IllegalArgumentException e) {
                return null;
            }
        }
        String key = p.key(n);
        String id = ids.get(key);
        if (id != null) {
            if (query(p, id) != null) return id;
            ids.remove(key);
        }
        String parent = find(p, n - 1);
        if (parent == null) return null;
        Path dir = new Path(p.tree, Arrays.copyOf(p.names, n - 1));
        if (children(dir, parent) == null) return null;
        id = ids.get(key);
        if (id != null) return id;
        // The same name in another case: storage that ignores case keeps one of them.
        String prefix = dir.key(n - 1) + "/";
        String want = p.names[n - 1].toLowerCase(Locale.ROOT);
        for (Map.Entry<String, String> e : ids.entrySet()) {
            String k = e.getKey();
            if (k.startsWith(prefix) && k.indexOf('/', prefix.length()) < 0
                    && k.substring(prefix.length()).toLowerCase(Locale.ROOT).equals(want))
                return e.getValue();
        }
        return null;
    }

    private String find(Path p) {
        return find(p, p.names.length);
    }

    /** Forgets `path` and everything under it. */
    private void forget(String path) {
        ids.remove(path);
        String prefix = path + "/";
        ids.keySet().removeIf(k -> k.startsWith(prefix));
    }

    synchronized String uri(String path) {
        Path p = split(path);
        String id = p != null ? find(p) : null;
        return id != null ? docUri(p, id).toString() : null;
    }

    /** "F" or "D", modified ms, ':', size; null when it is not there. */
    synchronized String info(String path) {
        Path p = split(path);
        String id = p != null ? find(p) : null;
        String[] q = id != null ? query(p, id) : null;
        if (q == null) return null;
        return (Document.MIME_TYPE_DIR.equals(q[0]) ? "D" : "F") + q[1] + ":" + q[2];
    }

    synchronized String[] list(String dir) {
        Path p = split(dir);
        String id = p != null ? find(p) : null;
        ArrayList<String> c = id != null ? children(p, id) : null;
        return c != null ? c.toArray(new String[0]) : null;
    }

    /** Makes the file or folder (and the folders above it); its URI, also when it was there. */
    synchronized String create(String path, boolean dir) {
        Path p = split(path);
        if (p == null) return null;
        return create(p, dir);
    }

    private String create(Path p, boolean dir) {
        String id = find(p);
        if (id != null) return docUri(p, id).toString();
        if (p.names.length == 0) return null;
        Path parent = p.parent();
        String parentId = find(parent);
        if (parentId == null) {
            if (create(parent, true) == null) return null;
            parentId = find(parent);
            if (parentId == null) return null;
        }
        try {
            Uri made = DocumentsContract.createDocument(resolver, docUri(parent, parentId),
                                                        dir ? Document.MIME_TYPE_DIR : FILE_TYPE, p.name());
            if (made == null) return null;
            // The provider names it (its own name when one was there after all: "GAME (1).sav").
            children(parent, parentId);
            String madeId = DocumentsContract.getDocumentId(made);
            if (!madeId.equals(ids.get(p.key(p.names.length))))
                Log.w(TAG, "Made " + p.name() + " under another name");
            return made.toString();
        } catch (Exception e) { // FileNotFoundException, or what the provider throws
            Log.w(TAG, "Cannot make " + p.key(p.names.length) + ": " + e);
            return null;
        }
    }

    synchronized boolean remove(String path) {
        Path p = split(path);
        String id = p != null && p.names.length > 0 ? find(p) : null;
        if (id == null) return false;
        try {
            boolean ok = DocumentsContract.deleteDocument(resolver, docUri(p, id));
            forget(p.key(p.names.length));
            return ok;
        } catch (Exception e) {
            Log.w(TAG, "Cannot remove " + path + ": " + e);
            forget(p.key(p.names.length));
            return false;
        }
    }

    /** Renames a file in its folder, replacing `to`. */
    synchronized boolean rename(String from, String to) {
        Path a = split(from), b = split(to);
        if (a == null || b == null || a.names.length == 0 || b.names.length == 0) return false;
        if (!a.tree.equals(b.tree) || !a.parent().key(a.names.length - 1).equals(b.parent().key(b.names.length - 1)))
            return false;
        String id = find(a);
        if (id == null) return false;
        String old = find(b);
        if (old != null && !old.equals(id)) {
            try {
                DocumentsContract.deleteDocument(resolver, docUri(b, old));
            } catch (Exception e) {
                Log.w(TAG, "Cannot replace " + to + ": " + e);
                return false;
            }
        }
        forget(a.key(a.names.length));
        forget(b.key(b.names.length));
        try {
            Uri now = DocumentsContract.renameDocument(resolver, docUri(a, id), b.name());
            if (now != null) return true;
        } catch (UnsupportedOperationException e) {
            return copy(a, id, b) && removeId(a, id); // a provider that renames nothing
        } catch (Exception e) {
            Log.w(TAG, "Cannot rename " + from + ": " + e);
            return false;
        }
        return find(b) != null;
    }

    private boolean copy(Path from, String id, Path to) {
        String made = create(to, false);
        if (made == null) return false;
        try (InputStream in = resolver.openInputStream(docUri(from, id));
             OutputStream out = resolver.openOutputStream(Uri.parse(made), "wt")) {
            if (in == null || out == null) return false;
            byte[] buf = new byte[65536];
            for (int n; (n = in.read(buf)) > 0;) out.write(buf, 0, n);
            return true;
        } catch (Exception e) {
            Log.w(TAG, "Cannot copy " + from.key(from.names.length) + ": " + e);
            return false;
        }
    }

    private boolean removeId(Path p, String id) {
        try {
            return DocumentsContract.deleteDocument(resolver, docUri(p, id));
        } catch (Exception e) {
            return false;
        }
    }

    /** The picked folder's name as its provider shows it; null when it cannot be read. */
    synchronized String treeName(String tree) {
        Path p = split(tree);
        String id = p != null ? find(p, 0) : null;
        if (id == null) return null;
        try (Cursor c = resolver.query(docUri(p, id), new String[] {Document.COLUMN_DISPLAY_NAME}, null, null, null)) {
            return c != null && c.moveToFirst() ? c.getString(0) : null;
        } catch (RuntimeException e) {
            return null;
        }
    }

    /** Whether the game keeps read and write access to the folder. */
    boolean hasAccess(String tree) {
        for (UriPermission u : resolver.getPersistedUriPermissions())
            if (u.getUri().toString().equals(tree) && u.isReadPermission() && u.isWritePermission()) return true;
        return false;
    }

    /** Keeps the access to a picked folder across runs (false when the provider doesn't). */
    boolean keep(Uri tree) {
        try {
            resolver.takePersistableUriPermission(tree, Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
            return true;
        } catch (SecurityException e) {
            Log.w(TAG, "Could not keep access to " + tree, e);
            return false;
        }
    }

    synchronized void release(String tree) {
        forget(tree);
        try {
            resolver.releasePersistableUriPermission(Uri.parse(tree),
                                                     Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        } catch (SecurityException e) {
            Log.i(TAG, "No access kept to " + tree);
        }
    }
}
