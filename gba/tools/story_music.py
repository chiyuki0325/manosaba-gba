"""Retain source BGM parameters in a compact, ROM-local command payload."""
import struct


class MusicCatalog:
    def __init__(self, docs):
        aliases = set()
        for doc in docs:
            for event in doc['events']:
                if event['op'] in ('PlayBgm', 'StopBgm'):
                    for key in ('BgmPath', 'IntroBgmPath'):
                        if value := event['params'].get(key):
                            aliases.add(value)
        self.aliases = [''] + sorted(aliases)
        self.ids = {value: index for index, value in enumerate(self.aliases)}
        self.contexts = []

    def command(self, doc, event):
        p = event.get('params', {})
        stop = event['op'] == 'StopBgm'
        keys = ('BgmPath', 'Volume', 'Loop', 'IntroBgmPath',
                'FadeOutDuration' if stop else 'Duration', 'FadeInDuration')
        parameters = sum(1 << bit for bit, key in enumerate(keys) if key in p)
        frames = lambda seconds: round(float(seconds) * 16777216 / 280896)
        path = self.ids.get(p.get('BgmPath', ''), 0)
        intro = self.ids.get(p.get('IntroBgmPath', ''), 0)
        volume = round(float(p.get('Volume', 1)) * 1024)
        duration = frames(p.get(keys[4], .35))
        fade = frames(p.get('FadeInDuration', 0))
        assert 0 <= volume <= 1024 and 0 <= duration < 65536 and 0 <= fade < 65536
        self.contexts.append(dict(path=doc['path'], line=event['line'], op=event['op'], params=p))
        return struct.pack('<6H3B', path, intro, volume, duration, fade, parameters,
                           bool(p.get('Loop', True)), bool(p.get('Wait', False)), stop)
