"""Source camera coverage, partial updates, source presets and 14-seat geometry."""
import unittest
from trial_views import catalog,rotation,focus,canonical,reduced_view,preset_view


class TrialViewsTest(unittest.TestCase):
    def test_partial_rotation_keeps_unspecified_axes(self):
        self.assertEqual(rotation([-1],{},[2,100,-4]),[-1,100,-4])
        self.assertEqual(rotation(', {360 / courtStandCount}',dict(courtStandCount=14),[2,10,-4]),[2,360/14,-4])

    def test_composition_uses_chapter_seat_count_and_tilt(self):
        state,actor=focus(dict(LookCharacterId='Yuki',Composition='Right',ZoomLevel=4,RollDirection='Left'),dict(yukiIdx=13),14,'Ema')
        shape,seat=canonical(state,14,1)
        self.assertEqual(actor,'Yuki');self.assertEqual(seat,13)
        self.assertEqual(shape,(1,13,5.6,0,round(-.1*360/14,5),4))

    def test_zoom_and_orbit_keep_full_source_state(self):
        rows={row['line']:row for row in catalog()['motions'] if row['path'].endswith('Act01_Chapter01_Trial00')}
        zoom=rows[142]
        self.assertEqual(zoom['start'],dict(height=5.,distance=10.,rotation=[1.,0.,0.]))
        self.assertEqual(zoom['end'],dict(height=5.5,distance=12.,rotation=[1.,0.,0.]))
        self.assertEqual(zoom['ease'],'OutSine');self.assertEqual(zoom['frames'],240)
        orbit=rows[82]
        self.assertEqual(orbit['start']['rotation'][::2],[-2,-2])
        self.assertEqual(orbit['end']['rotation'][::2],[-1,-2])
        self.assertEqual((orbit['start']['height'],orbit['end']['height']),(3,4))

    def test_named_presets_merge_zoom2_and_tilt_only(self):
        self.assertEqual(preset_view(dict(Composition='Center',ZoomLevel=2)),preset_view(dict(Composition='Center',ZoomLevel=1)))
        self.assertEqual(preset_view(dict(Composition='Left',ZoomLevel=3,RollDirection='Right')),8)
        self.assertEqual(preset_view(dict(Composition='Right',ZoomLevel=4)),12)

    def test_small_source_offsets_choose_the_nearest_composition(self):
        self.assertEqual(reduced_view(dict(distance=12,height=5.5,rotation=[0,.02*360/13,-4]),13),7)
        self.assertEqual(reduced_view(dict(distance=12,height=5.5,rotation=[0,.08*360/13,-4]),13),8)


if __name__=='__main__':unittest.main()
