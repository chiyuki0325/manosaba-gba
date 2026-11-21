"""Entry inference regressions: live branches, debug paths, cross-chapter isolation."""
import struct,unittest
from story import OPS
from trial_effects import entry_states,NEUTRAL,shake_command

def emit(op,payload=b''):return bytes([OPS[op]])+struct.pack('<H',len(payload))+payload
def jump(script,pc=0):return emit('jump',struct.pack('<HI',script,pc))
def overlay(color,alpha):return emit('screen',b'\0'+struct.pack('<HHH',color,alpha,0)+b'\0')
def docs(count):return [dict(path=f'Act02_Chapter05/Act02_Chapter05_Trial{i:02}') for i in range(count)]
END=emit('end',b'\0')

class EffectsTest(unittest.TestCase):
    def test_fire_and_color_follow_real_jumps(self):
        codes=[emit('trial',b'\x0b\x01')+overlay(31,128)+jump(2),END,
               emit('trial',b'\x0b\x00')+jump(3),END]
        seeds,_=entry_states(docs(4),codes,OPS)
        self.assertEqual(seeds[1],NEUTRAL)  # Sorted filename is not a predecessor.
        self.assertEqual(seeds[2],(31,128,1,None))
        self.assertEqual(seeds[3],(31,128,0,None))
    def test_ambiguous_branches_are_reported(self):
        a=emit('trial',b'\x0b\x01')+jump(1);b=emit('trial',b'\x0b\x00')+jump(1)
        branch=emit('branch',b'\x01'+struct.pack('<HI',1,10+len(a)))
        seeds,report=entry_states(docs(2),[branch+a+b,END],OPS)
        self.assertEqual(seeds[1][2],0)
        self.assertEqual(report[0]['ambiguous'],dict(fire=[0,1]))
    def test_debug_path_does_not_pollute_live_entry(self):
        bad=emit('trial',b'\x0b\x00')+jump(1);good=emit('trial',b'\x0b\x01')+jump(1)
        branch=emit('test',b'\x03\xff'+struct.pack('<I',9+len(bad)))
        seeds,_=entry_states(docs(2),[branch+bad+good,END],OPS)
        self.assertEqual(seeds[1][2],1)
    def test_shake_and_reset(self):
        continuous=shake_command(dict(Force=[.01,-.01],Duration=180,Frequency=1800,DampingRatio=0))
        impact=shake_command(dict(Force=[.4,-.2],Duration=3,Frequency=50,DampingRatio=1))
        codes=[emit('trial',continuous)+jump(1),emit('trial',impact)+jump(2),END]
        seeds,_=entry_states(docs(3),codes,OPS)
        self.assertEqual(seeds[1][3],(10800,1,1,6,0));self.assertIsNone(seeds[2][3])
    def test_chapter_isolation(self):
        documents=docs(2);documents[1]['path']='Act02_Chapter06/Act02_Chapter06_Trial00'
        seeds,_=entry_states(documents,[emit('trial',b'\x0b\x01')+jump(1),END],OPS)
        self.assertEqual(seeds[1],NEUTRAL)
    def test_objection_resume_carries_effects(self):
        # Debate skip and objection return meet before the next-script jump.
        payload=b'\0'+struct.pack('<H',300)+bytes([12,30,228,100])+b'text\0\x01'+struct.pack('<HHHI',0,3,1,0)
        codes=[emit('debate',payload)+jump(2),emit('trial',b'\x0b\x01')+emit('resume'),END]
        _,report=entry_states(docs(3),codes,OPS)
        self.assertIn(dict(fire=[0,1]),[r['ambiguous'] for r in report])

if __name__=='__main__':unittest.main()
