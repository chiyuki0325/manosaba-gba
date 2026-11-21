#!/usr/bin/env python3
"""Golden reflow examples plus source-content/line-width checks for all dialogue."""
import unittest
from story_layout import reflow, clean_text, pixel_width, catalog, PROFILES, NO_START, NO_END, source_paragraphs


class StoryLayoutTest(unittest.TestCase):
    def test_original_break_is_soft(self):
        source = '这是一句很长的话，按原作的屏幕宽度被<br>\n分隔成了两行。'
        self.assertEqual(reflow(source,198), '这是一句很长的话，按原作的屏幕宽\n度被分隔成了两行。')

    def test_fitting_original_lines_keep_breaks(self):
        self.assertEqual(reflow('甲乙丙<br>丁',36),'甲乙丙\n丁')
        self.assertEqual(reflow('甲乙\n丙丁',36),'甲乙\n丙丁')

    def test_only_overflowing_line_loses_following_break(self):
        self.assertEqual(reflow('甲乙丙丁<br>戊<br>己庚',36),'甲乙丙\n丁戊\n己庚')
        self.assertEqual(reflow('短行<br>甲乙丙丁<br>戊己庚辛<br>壬',36),'短行\n甲乙丙\n丁戊己\n庚辛壬')
        self.assertEqual(reflow('abcdefg<br>hi<br>bye',36),'abcdef\ng hi\nbye')

    def test_html_break_variants(self):
        self.assertEqual(clean_text('甲<br/>\r\n乙<BR />丙\n丁'), '甲乙丙丁')

    def test_preserve_paragraphs(self):
        self.assertEqual(reflow('甲<br>\n<br>\n乙\n\n丙',24), '甲\n\n乙\n\n丙')

    def test_english_words_and_spacing(self):
        self.assertEqual(clean_text('This is a<br>\nlong line.'), 'This is a long line.')
        self.assertEqual(reflow('hello world',36), 'hello\nworld')
        self.assertEqual(reflow('ABCDEFGHI',24), 'ABCD\nEFGH\nI')
        self.assertEqual(reflow('甲ABCDEFGHI',36), '甲ABCD\nEFGHI')

    def test_punctuation(self):
        self.assertEqual(reflow('甲乙丙，丁戊',36), '甲乙\n丙，丁\n戊')
        self.assertEqual(reflow('甲乙（丙丁）',36), '甲乙\n（丙\n丁）')
        self.assertEqual(reflow('甲乙……丙',36), '甲\n乙……\n丙')

    def test_markup_and_entities(self):
        self.assertEqual(clean_text('<color=#fff>甲</color>[wait 1]<br>乙 &amp; 丙'), '甲乙 & 丙')

    def test_optional_and_emphatic_source_breaks(self):
        from story_layout import dialogue_text
        source='可是，她被无形的锁链束缚着，<br>就像个囚犯一样，连捂紧耳朵也做不到。'
        marked=dialogue_text(source,'Act01_Chapter01/Act01_Chapter01_Adv01')
        self.assertIn('\r',marked)
        self.assertEqual(marked.replace('\r',''),clean_text(source))
        self.assertEqual(reflow('不对。<br>不是这样。<br>绝对不是。',198,marked=True),'不对。\n不是这样。\n绝对不是。')
        self.assertEqual(reflow('甲<br><br>乙',198,marked=True),'甲\n\n乙')

    def test_complete_corpus(self):
        records = catalog()
        for r in records:
            box=PROFILES[r['profile']]
            width=box['right']-box['left']
            # Formatting must never remove or reorder printable story content.
            self.assertEqual(r['text'].replace('\n','').replace(' ',''),
                             clean_text(r['source']).replace('\n','').replace(' ',''),
                             (r['script'],r['line']))
            self.assertLessEqual(len(r['text'].encode())+1,384)
            originals=[p for paragraph in source_paragraphs(r['source']) for p in paragraph]
            preserved_starts={p[0] for p in originals}
            preserved_ends={p[-1] for p in originals if pixel_width(p)<=width}
            lines=r['text'].split('\n')
            for index,line in enumerate(lines):
                self.assertLessEqual(pixel_width(line),width,(r['script'],r['line'],line))
                if line:
                    if index and lines[index-1]:
                        if line[0] not in preserved_starts:
                            self.assertNotIn(line[0],NO_START,(r['script'],r['line'],line))
                    if line[-1] not in preserved_ends:
                        self.assertNotIn(line[-1],NO_END,(r['script'],r['line'],line))


if __name__=='__main__':
    unittest.main()
