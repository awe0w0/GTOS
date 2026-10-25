// Artifact identity is independent of the spelling or location of a source file.
export function artifactLayout(workRoot, identities) {
  if (typeof workRoot !== 'string' || !new RegExp('^/var/tmp/[a-z0-9][a-z0-9_.-]*$').test(workRoot) || workRoot.includes('..')) {
    throw new Error('canonical native work root required');
  }
  if (!Array.isArray(identities) || identities.length === 0) throw new Error('artifact identities required');
  const seen = new Set();
  const entries = identities.map(identity => {
    if (!identity || typeof identity.slug !== 'string' || !/^[a-z][a-z0-9_]*(?:\.asm)?$/.test(identity.slug)) {
      throw new Error('invalid artifact slug');
    }
    if (seen.has(identity.slug)) throw new Error('duplicate artifact slug');
    if (typeof identity.has_depfile !== 'boolean') throw new Error('explicit dependency-file presence required');
    seen.add(identity.slug);
    const objectName = identity.slug + '.o';
    const depName = identity.slug + '.d';
    return {
      slug: identity.slug,
      staged_linux: workRoot + '/inherited/' + objectName,
      depfile_linux: identity.has_depfile ? workRoot + '/inherited/' + depName : null,
      object_destination: 'objects/reused/' + objectName,
      depfile_destination: identity.has_depfile ? 'objects/reused/' + depName : null
    };
  });
  return {
    entries,
    object_inputs: entries.map(entry => entry.staged_linux),
    required_paths: entries.flatMap(entry => entry.depfile_destination ? [entry.object_destination, entry.depfile_destination] : [entry.object_destination])
  };
}
